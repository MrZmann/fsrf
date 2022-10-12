#include <assert.h>
#include <iostream>
#include <sys/mman.h>

#include "fsrf.h"

FSRF *fsrf;

void FSRF::cntrlreg_write(uint64_t addr, uint64_t value)
{
    fpga.write_app_reg(app_id, addr, value);
}

uint64_t FSRF::cntrlreg_read(uint64_t addr)
{
    uint64_t value;
    fpga.read_app_reg(app_id, addr, value);
    return value;
}

uint64_t FSRF::allocate_device_ppn()
{
    uint64_t addr = phys_base;

    for (auto ppn : allocated_device_ppns)
    {
        // We can map this page!
        if (ppn >= addr + (1 << 12))
            break;
        else
        {
            addr += (1 << 12);
            if (addr == phys_bound)
            {
                std::cerr << "Not enough free pages on FPGA" << std::endl;
                exit(1);
            }
        }
    }

    allocated_device_ppns.insert(addr);

    return addr >> 12;
}

void FSRF::free_device_vpn(uint64_t vpn)
{
    allocated_device_ppns.erase(device_vpn_to_ppn[vpn]);
    device_vpn_to_ppn.erase(vpn);
}

uint64_t FSRF::read_tlb_fault()
{
    uint64_t res;
    fpga.read_sys_reg(app_id, 0, res);
    return res;
}

void FSRF::respond_tlb(uint64_t ppn, uint64_t valid)
{
    uint64_t resp = 0 | (ppn << 1) | valid;
    fpga.write_sys_reg(app_id, 0x0, resp);
}

void FSRF::write_tlb(uint64_t vpn,
                     uint64_t ppn,
                     bool writeable,
                     bool readable,
                     bool present,
                     bool huge)
{
    assert(!huge);

    // Max of 36 vpn bits
    assert(vpn < ((uint64_t) 1 << 36));
    // Max of 24 ppn bits
    assert(ppn < (1 << 24));

    uint64_t tlb_addr = dram_tlb_addr(vpn);
    uint64_t entry = (vpn << 28) | (ppn << 4) | (writeable << 2) | (readable << 1) | present;

    fpga.write_mem_reg(tlb_addr, entry);
}

uint64_t FSRF::dram_tlb_addr(uint64_t vpn)
{
    // these determine the set
    const uint64_t app_offsets[4] = {0, 32ull << 30, 16ull << 30, 48ull << 30};
    const uint64_t tlb_bits = 21;
    uint64_t vpn_index = vpn & ((1 << tlb_bits) - 1);

    // this determines the way
    uint64_t vpn_offset = (vpn >> tlb_bits) & 0x7;
    uint64_t dram_addr = vpn_index * 64 + app_offsets[app_id] + vpn_offset * 8;
    return dram_addr;
}

void FSRF::handle_device_fault(bool read, uint64_t vpn)
{
    // TODO: check permissions of vpn on the host
    // for now assume R/W on my vpn

    // if permissions are readable or writeable
    // set to read only while we copy it over
    // this ensures no one is editing during the DMA
    uint64_t vaddr = vpn << 12;
    uint64_t bytes = 1 << 12;

    std::cout << "Handling device fault at: " << vaddr << '\n';

    // PROT_NONE allows only fpga to access (fpga accessing physical memory)
    mprotect((void*) vaddr, bytes, PROT_NONE);

    uint64_t device_ppn = allocate_device_ppn();
    fpga.dma_write((void*) vaddr, device_ppn << 12, bytes);

    // remember that this is on the device
    device_vpn_to_ppn[vpn] = device_ppn;
    write_tlb(vpn, device_ppn, true, true, true, false);
    respond_tlb(device_ppn, true);
}

void FSRF::device_fault_listener()
{
    std::cout << "fault handler is listening";

    while (true)
    {
        uint64_t fault = read_tlb_fault();
        if(fault != 1062849512059437056)
            std::cout << "before fault " << fault << "\n";
        if (!(fault & 1) || fault == (uint64_t) -1)
            continue;

        std::cout << "fault " << fault << "\n";

        bool read = fault & 0x2;
        uint64_t vpn = (fault >> 2) & 0xFFFFFFFFFFFFF;

        handle_device_fault(read, vpn);
    }
}

void FSRF::handle_host_fault(int sig, siginfo_t *info, void *ucontext)
{
    assert(sig == SIGSEGV);
    uint64_t missAddress = (uint64_t)info->si_addr;
    uint64_t vpn = missAddress >> 12;

    // if this page is on the device
    if (fsrf->device_vpn_to_ppn.find(vpn) != fsrf->device_vpn_to_ppn.end())
    {
        uint64_t vaddr = vpn << 12;

        // invalidate on tlb
        fsrf->write_tlb(vpn, fsrf->device_vpn_to_ppn[vpn], false, false, false, false);
        // dma from device to host
        fsrf->fpga.dma_read((void*) vaddr, fsrf->device_vpn_to_ppn[vpn], (uint64_t) 1 << 12);

        // free up device page
        fsrf->free_device_vpn(vpn);

        mprotect((void*) vaddr, 1 << 12, PROT_READ | PROT_WRITE);
        return;
    }

    // Page wasn't supposed to be accessible after all
    std::cerr << "Host tried to access illegal address: " << info->si_addr << '\n';
    int raiseError = *(int *)info->si_addr;
    std::cerr << "This should never print: " << raiseError << '\n';
}
