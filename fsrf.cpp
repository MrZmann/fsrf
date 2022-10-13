#include <assert.h>
#include <iostream>
#include <sys/mman.h>

#include "fsrf.h"

FSRF *fsrf;

FSRF::FSRF(uint64_t app_id) : fpga(0, app_id), app_id(app_id)
{
    // Global instance for the SIGSEGV handler to use
    fsrf = this;
    faultHandlerThread = std::thread(&FSRF::device_fault_listener, this);

    // enable tlb
    fpga.write_sys_reg(app_id, 0x10, 1);

    // use dram tlb
    fpga.write_sys_reg(app_id, 0x18, 1);

    // Coyote striping
    fpga.write_sys_reg(app_id + 4, 0x10, 0x0);

    // PCIe coyote striping
    fpga.write_sys_reg(8, 0x18, 0x0);

    struct sigaction act = {0};
    act.sa_sigaction = FSRF::handle_host_fault;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &act, NULL);

    uint64_t addrs[4] = {0, 8 << 20, 4 << 20, 12 << 20};
    phys_base = addrs[app_id];
    phys_bound = addrs[app_id] + (16 << 20) / max_apps;

    flush_tlb();
    std::cout << "Finished flush\n";
}


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
    uint64_t res = (uint64_t) ~0;
    fpga.read_sys_reg(app_id, 0, res);
    return res;
}

void FSRF::respond_tlb(uint64_t ppn, uint64_t valid)
{
    uint64_t resp = 0 | (ppn << 1) | valid;
    fpga.write_sys_reg(app_id, 0x0, resp);
}

void FSRF::flush_tlb(){
    uint64_t low_addr = dram_tlb_addr(0);
    uint64_t high_addr = dram_tlb_addr((uint64_t) 1 << 36);
    uint64_t* bytes = new uint64_t[512];

    for(uint64_t ppn = low_addr; ppn <= high_addr; ppn++){
        fpga.dma_write(bytes, ppn << 12, 0x1000);
    }
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

    std::cout << "Handling device fault at: " << (uint64_t*) vaddr << '\n';

    // PROT_NONE allows only fpga to access (fpga accessing physical memory)
    mprotect((void*) vaddr, bytes, PROT_READ);

    uint64_t device_ppn = allocate_device_ppn();
    std::cout << "Attempting dma write\n";
    fpga.dma_write((void*) vaddr, device_ppn << 12, bytes); // appears that the host access vaddr here.
    std::cout << "Finished dma write\n";

    mprotect((void*) vaddr, bytes, PROT_NONE);
    // remember that this is on the device
    device_vpn_to_ppn[vpn] = device_ppn;
    std::cout << "keys:\n";
    for(auto i : device_vpn_to_ppn){
        std::cout << "\t" << i.first << "\n";
    }
    write_tlb(vpn, device_ppn, true, true, true, false);
    respond_tlb(device_ppn, true);
}

void FSRF::device_fault_listener()
{
    while (true)
    {
        uint64_t fault = read_tlb_fault();
        //if(fault != 1062849512059437056 && fault != 0)
        //    std::cout << "before fault " << fault << "\n";
        if (!(fault & 1) || fault == (uint64_t) -1)
            continue;

        std::cout << "fault " << (uint64_t*) fault << "\n";

        bool read = fault & 0x2;
        uint64_t vpn = (fault >> 2) & 0xFFFFFFFFFFFFF;

        handle_device_fault(read, vpn);
    }
    std::cerr<< "fault listener should never return!\n";
}

void FSRF::handle_host_fault(int sig, siginfo_t *info, void *ucontext)
{
    assert(sig == SIGSEGV);
    uint64_t missAddress = (uint64_t)info->si_addr;
    uint64_t vpn = missAddress >> 12;

    std::cerr << "Host trying to access address: " << info->si_addr << '\n';
    std::cerr << "Host trying to access vpn: " << vpn << '\n';

    std::cout << "Here are all the pages on the device (vpn):\n";
    for(auto i : fsrf->device_vpn_to_ppn){
        std::cout << "\t" << i.first << "\n";
    }

    // if this page is on the device
    if (fsrf->device_vpn_to_ppn.find(vpn) != fsrf->device_vpn_to_ppn.end())
    {
        uint64_t vaddr = vpn << 12;

        // invalidate on tlb
        fsrf->write_tlb(vpn, fsrf->device_vpn_to_ppn[vpn], false, false, false, false);

        mprotect((void*) vaddr, 1 << 12, PROT_READ | PROT_WRITE);
        // dma from device to host
        fsrf->fpga.dma_read((void*) vaddr, fsrf->device_vpn_to_ppn[vpn], (uint64_t) 1 << 12);

        // free up device page
        fsrf->free_device_vpn(vpn);
        return;
    }

    // Page wasn't supposed to be accessible after all
    std::cerr << "Host tried to access illegal address: " << info->si_addr << '\n';
    int raiseError = *(int *)info->si_addr;
    std::cerr << "This should never print: " << raiseError << '\n';
}
