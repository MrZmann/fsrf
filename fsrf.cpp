#include <assert.h>
#include <iostream>
#include <sys/mman.h>

#include "fsrf.h"

#define DBG(x)       \
    if (fsrf->debug) \
    std::cout << "[" << __FUNCTION__ << ":" << __LINE__ << "]\t" << x << std::endl
#define ERR(x)                                                                          \
    {                                                                                   \
        std::cerr << "[" << __FUNCTION__ << ":" << __LINE__ << "]\t" << x << std::endl; \
        exit(1);                                                                        \
    }
#define PAGE_SIZE 0x1000
// Global instance for the SIGSEGV handler to use
FSRF *fsrf = nullptr;

FSRF::FSRF(uint64_t app_id, MODE mode, bool debug) : debug(debug),
                                                     mode(mode),
                                                     fpga(0, app_id),
                                                     lock(),
                                                     app_id(app_id)
{
    if (fsrf != nullptr)
    {
        ERR("Global fsrf object already initialized");
    }
    fsrf = this;

    if (app_id > 3)
        ERR("app_id must be in range [0, 3]\nGiven: " << app_id);
    if (mode == FSRF::MODE::NONE)
        ERR("Mode must not be none");
    DBG("app_id: " << app_id);
    DBG("mode: " << mode_str(mode));

    faultHandlerThread = std::thread(&FSRF::device_fault_listener, this);

    fpga.write_sys_reg(app_id, 0x10, 1);       // enable tlb
    fpga.write_sys_reg(app_id, 0x18, 1);       // use dram tlb
    fpga.write_sys_reg(app_id + 4, 0x10, 0x0); // Coyote striping
    fpga.write_sys_reg(8, 0x18, 0x0);          // PCIe coyote striping

    DBG("Registering host signal handler");

    struct sigaction act = {0};
    act.sa_sigaction = FSRF::handle_host_fault;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &act, NULL);

    uint64_t addrs[4] = {0, 8 << 20, 4 << 20, 12 << 20};
    // offset 128 MB for TLB
    phys_base = (128 << 20) + addrs[app_id];
    phys_bound = addrs[app_id] + (16 << 20) / max_apps;

    flush_tlb();
}

FSRF::~FSRF()
{
    abort = true;
    faultHandlerThread.join();
}

const char *FSRF::mode_str(MODE mode)
{
    constexpr const char *mode_string[3] = {"inv_read", "inv_write", "mmap"};
    return mode_string[mode];
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

void *FSRF::fsrf_malloc(uint64_t length, uint64_t host_permissions, uint64_t device_permissions)
{
    const std::lock_guard<std::mutex> guard(lock);

    assert(mode == MMAP);
    void *res = mmap(0, length, host_permissions, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (res == MAP_FAILED)
    {
        DBG("mmap failed");
        return res;
    }

    DBG("mmap returned pointer: " << res);
    uint64_t addr = (uint64_t)res;
    uint64_t size = (length % PAGE_SIZE) ? length + (PAGE_SIZE - length % PAGE_SIZE) : length;
    DBG("Original size: " << length << ", new size: " << size);
    assert(length % PAGE_SIZE == 0);
    VME vme{addr, size, device_permissions, nullptr};
    vmes[addr] = vme;
    return res;
}

void FSRF::sync_device_to_host(uint64_t *addr, size_t length)
{
    assert(mode == MMAP);
    const std::lock_guard<std::mutex> guard(lock);
    uint64_t low = (uint64_t)addr;
    uint64_t high = low + (uint64_t)length;
    auto it = vmes.begin();
    while (it != vmes.end())
    {
        VME vme = it->second;
        // intervals overlap
        if ((low >= vme.addr && low < (vme.addr + vme.size)) ||
            (high > vme.addr && high < (vme.addr + vme.size)))
        {
            // unmap from addr to addr + size
            // if the user wanted this data written to host, they should have called msync.
            for (uint64_t vpn = vme.addr; vpn < vme.addr + vme.size; ++vpn)
            {
                uint64_t vaddr = vpn << 12;
                // this page was never put on the device
                if (device_vpn_to_ppn.find(vpn) == device_vpn_to_ppn.end())
                    continue;

                write_tlb(vpn, device_vpn_to_ppn[vpn], false, false, false, false);
                mprotect((void *)vaddr, 1 << 12, PROT_READ | PROT_WRITE);

                DBG("Reading " << (uint64_t *)vaddr << " from fpga to host");

                // dma from device to host
                fpga.dma_read((void *)vaddr, device_vpn_to_ppn[vpn] << 12, (uint64_t)1 << 12);

                DBG("Finished dma read");

                // free up device page
                fsrf->free_device_vpn(vpn);
            }
            it = vmes.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void FSRF::fsrf_free(uint64_t *addr)
{
    assert(mode == MMAP);
    auto it = vmes.begin();
    while (it != vmes.end())
    {
        VME vme = it->second;
        if ((uint64_t)addr >= vme.addr && (uint64_t)addr < (vme.addr + vme.size))
        {
            // unmap from addr to addr + size
            // if the user wanted this data written to host, they should have called msync.
            for (uint64_t vpn = vme.addr; vpn < vme.addr + vme.size; ++vpn)
            {
                // this page was never put on the device
                if (device_vpn_to_ppn.find(vpn) == device_vpn_to_ppn.end())
                    continue;

                write_tlb(vpn, device_vpn_to_ppn[vpn], false, false, false, false);
                // No DMA back on free

                // free up device page
                fsrf->free_device_vpn(vpn);
            }
            it = vmes.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

uint64_t FSRF::allocate_device_ppn()
{
    uint64_t addr = phys_base;

    for (auto ppn : allocated_device_ppns)
    {
        // We can map this page!
        if (ppn >= addr + PAGE_SIZE)
            break;
        else
        {
            addr += PAGE_SIZE;
            if (addr == phys_bound)
            {
                ERR("Not enough free pages on FPGA");
            }
        }
    }

    DBG("addr: " << addr);

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
    uint64_t res = (uint64_t)~0;
    fpga.read_sys_reg(app_id, 0, res);
    return res;
}

void FSRF::respond_tlb(uint64_t ppn, uint64_t valid)
{
    uint64_t resp = 0 | (ppn << 1) | valid;
    fpga.write_sys_reg(app_id, 0x0, resp);
}

void FSRF::flush_tlb()
{
    DBG("Flushing tlb");
    uint64_t low_addr = dram_tlb_addr(0);
    uint64_t *bytes = new uint64_t[512];

    for (uint64_t ppn = low_addr; ppn <= 32768; ppn++)
    {
        fpga.dma_write(bytes, ppn << 12, PAGE_SIZE);
    }
}

void FSRF::write_tlb(uint64_t vpn,
                     uint64_t ppn,
                     uint64_t writeable,
                     uint64_t readable,
                     uint64_t present,
                     uint64_t huge)
{
    assert(!huge);

    // Max of 36 vpn bits
    assert(vpn < ((uint64_t)1 << 36));
    // Max of 24 ppn bits
    assert(ppn < (1 << 24));

    uint64_t tlb_addr = dram_tlb_addr(vpn);
    uint64_t entry = (vpn << 28) | (ppn << 4) | (writeable << 2) | (readable << 1) | present;
    DBG("Entry " << (void *)entry);

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

    DBG("Handling device fault at: " << (uint64_t *)vaddr);

    // make sure the host fault handler isn't messing with my data structures
    // at the same time
    const std::lock_guard<std::mutex> guard(lock);

    if (mode == MODE::INV_READ)
    {
        mprotect((void *)vaddr, bytes, PROT_READ);

        // find a place to put the data
        uint64_t device_ppn = allocate_device_ppn();
        // put the data there
        fpga.dma_write((void *)vaddr, device_ppn << 12, bytes);
        // remember where we put it
        device_vpn_to_ppn[vpn] = device_ppn;

        // make it inaccessible on the host
        mprotect((void *)vaddr, bytes, PROT_NONE);

        // create a tlb entry
        write_tlb(vpn, device_ppn, /*writeable*/ true, true, true, false);

        // respond to the fault
        respond_tlb(device_ppn, true);
    }
    else if (mode == MODE::INV_WRITE)
    {
        mprotect((void *)vaddr, bytes, PROT_READ);

        // If we are reading, we need to make it readable on the host and device
        if (read)
        {
            // it is already readonly on the host at this point

            // find a place to put the data
            uint64_t device_ppn = allocate_device_ppn();
            // put the data there
            fpga.dma_write((void *)vaddr, device_ppn << 12, bytes);
            // remember where we put it
            device_vpn_to_ppn[vpn] = device_ppn;

            // create a tlb entry
            write_tlb(vpn, device_ppn, /*writeable*/ true, true, true, false);

            // respond to the fault
            respond_tlb(device_ppn, true);
        }
        // if it's a write, there are two cases.
        // either the page is already readable on the device, or this is a blind write.
        else
        {
            // the page is already on the device, we just need to invalidate it on the host
            // and make it writeable on the device
            if (device_vpn_to_ppn.find(vpn) == device_vpn_to_ppn.end())
            {
                mprotect((void *)vaddr, bytes, PROT_NONE);
                write_tlb(vpn, device_vpn_to_ppn[vpn], /*writeable*/ true, true, true, false);
                respond_tlb(device_vpn_to_ppn[vpn], true);
            }
            // we need to allocate a page on the device. invalidate on host. RW on device
            else
            {
                // find a place to put the data
                uint64_t device_ppn = allocate_device_ppn();
                // put the data there
                fpga.dma_write((void *)vaddr, device_ppn << 12, bytes);
                // remember where we put it
                device_vpn_to_ppn[vpn] = device_ppn;

                // create a tlb entry
                write_tlb(vpn, device_ppn, /*writeable*/ true, true, true, false);

                // invalidate on host
                mprotect((void *)vaddr, bytes, PROT_NONE);

                // respond to the fault
                respond_tlb(device_ppn, true);
            }
        }
    }
    else
    {
        assert(mode == MODE::MMAP);
        uint64_t device_ppn = -1;
        bool valid_addr = false;
        VME vme;
        for (auto it = vmes.begin(); it != vmes.end(); ++it)
        {
            vme = it->second;
            // This is a valid mmapped address
            if (vaddr >= vme.addr && vaddr < vme.addr + vme.size)
            {
                DBG("Found suitable VME");
                device_ppn = allocate_device_ppn();
                fpga.dma_write((void *)vaddr, device_ppn << 12, bytes);
                valid_addr = true;
                break;
            }
        }
        if (!valid_addr)
            ERR("Invalid device access");

        // remember where we put it
        device_vpn_to_ppn[vpn] = device_ppn;

        // both host and device can read / write
        // TODO actually use VME permissions
        int res = mprotect((void *)vaddr, bytes, PROT_READ | PROT_WRITE);
        if (res)
        {
            ERR("mprotect failed");
        }
        write_tlb(vpn, device_ppn, /*writeable*/ true, true, true, false);
        respond_tlb(device_ppn, true);
    }
}

void FSRF::device_fault_listener()
{
    DBG("Starting up");
    while (true)
    {

        uint64_t fault = read_tlb_fault();

        assert(fault != (uint64_t)-1);
        if (!(fault & 1))
        {
            uint64_t num_credits = fault >> 57;
            if (abort && num_credits == 0)
                return;
            continue;
        }
        DBG("Found fault");

        bool read = fault & 0x2;
        if (read)
        {
            DBG("Device tried to read");
        }
        else
        {
            DBG("Device tried to write");
        }

        uint64_t vpn = (fault >> 2) & 0xFFFFFFFFFFFFF;

        DBG("vpn: " << (void *)vpn);

        handle_device_fault(read, vpn);
    }
    ERR("Fault listener should never return!");
}

void FSRF::handle_host_fault(int sig, siginfo_t *info, void *ucontext)
{
    assert(sig == SIGSEGV);
    uint64_t missAddress = (uint64_t)info->si_addr;
    uint64_t err = ((ucontext_t *)ucontext)->uc_mcontext.gregs[REG_ERR];
    bool write_fault = !(err & 0x2);
    uint64_t vpn = missAddress >> 12;

    DBG("Host trying to access address: " << info->si_addr);

    const std::lock_guard<std::mutex> guard(fsrf->lock);

    // if this page is on the device
    if (fsrf->device_vpn_to_ppn.find(vpn) != fsrf->device_vpn_to_ppn.end())
    {
        uint64_t vaddr = vpn << 12;

        if (fsrf->mode == MODE::INV_READ || (write_fault && fsrf->mode == MODE::INV_WRITE))
        {
            DBG("Removing " << (uint64_t *)vaddr << " from fpga tlb");

            // invalidate on tlb
            fsrf->write_tlb(vpn, fsrf->device_vpn_to_ppn[vpn], false, false, false, false);
            mprotect((void *)vaddr, 1 << 12, PROT_READ | PROT_WRITE);

            DBG("Reading " << (uint64_t *)vaddr << " from fpga to host");

            // dma from device to host
            fsrf->fpga.dma_read((void *)vaddr, fsrf->device_vpn_to_ppn[vpn] << 12, (uint64_t)1 << 12);

            DBG("Finished dma read");

            // free up device page
            fsrf->free_device_vpn(vpn);
        }
        else if (fsrf->mode == MODE::INV_WRITE)
        {
            DBG("Marking " << (uint64_t *)vaddr << " as readonly on fpga tlb");
            // set to readonly on TLB
            fsrf->write_tlb(vpn, fsrf->device_vpn_to_ppn[vpn], false, true, true, false);

            mprotect((void *)vaddr, 1 << 12, PROT_READ | PROT_WRITE);

            // dma from device to host
            // we have to dma because the device has written to this page
            fsrf->fpga.dma_read((void *)vaddr, fsrf->device_vpn_to_ppn[vpn] << 12, (uint64_t)1 << 12);

            DBG("Finished dma read");
            // set as readonly on host
            mprotect((void *)vaddr, 1 << 12, PROT_READ);
        }
        else
        {
            ERR("MMAP should not have host faults, something is wrong");
        }
        return;
    }

    // Page wasn't supposed to be accessible after all
    ERR("Host tried to access illegal address: " << info->si_addr);
}
