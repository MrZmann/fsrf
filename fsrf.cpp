#include <assert.h>
#include <chrono>
#include <iostream>
#include <sys/mman.h>

#include "fsrf.h"

using namespace std::chrono;

#define ERR(x)                                                                          \
    {                                                                                   \
        std::cerr << "[" << __FUNCTION__ << ":" << __LINE__ << "]\t" << x << std::endl; \
        exit(1);                                                                        \
    }

#ifdef DEBUG
#define DBG(x)       \
    if (fsrf->debug) \
    std::cout << "[" << __FUNCTION__ << ":" << __LINE__ << "]\t" << x << std::endl
#define ASSERT(b) assert(b)
#else
#define DBG(x) \
    {          \
    }
#define ASSERT(b) \
    {             \
    }
#endif

#ifdef PERF
#define TRACK(name)                                                      \
    {                                                                    \
        fsrf->cumulative_times[name] = std::chrono::nanoseconds::zero(); \
        fsrf->num_calls[name] = 0;                                       \
    }

#define START(name)                                                                \
    {                                                                              \
        ASSERT(fsrf->cumulative_times.find(name) != fsrf->cumulative_times.end()); \
        fsrf->last_start[name] = high_resolution_clock::now();                     \
        fsrf->num_calls[name] += 1;                                                \
    }

#define END(name)                                                                  \
    {                                                                              \
        ASSERT(fsrf->cumulative_times.find(name) != fsrf->cumulative_times.end()); \
        ASSERT(fsrf->last_start.find(name) != fsrf->last_start.end());             \
        auto end = high_resolution_clock::now();                                   \
        fsrf->cumulative_times[name] += end - fsrf->last_start[name];              \
    }
#else
#define TRACK(name) \
    {               \
    }
#define START(name) \
    {               \
    }
#define END(name) \
    {             \
    }
#endif

#define PAGE_SIZE 0x1000
// Global instance for the SIGSEGV handler to use
FSRF *fsrf = nullptr;

FSRF::FSRF(uint64_t app_id, MODE mode, bool debug, int batch_size) : debug(debug),
                                                                     app_id(app_id),
                                                                     mode(mode),
                                                                     fpga(0, app_id, dram_tlb_addr(0)),
                                                                     num_credits(0),
                                                                     lock(),
                                                                     mmap_dma_size(batch_size * 0x1000)
{
    if (fsrf != nullptr)
    {
        ERR("Global fsrf object already initialized");
    }
    fsrf = this;

    TRACK("MMAP");
    TRACK("MPROTECT");
    TRACK("MPROTECT_NONE");
    TRACK("MPROTECT_NONE_TO_R");
    TRACK("MPROTECT_NONE_TO_RW");
    TRACK("MPROTECT_RW_TO_R");

    if (app_id > 3)
        ERR("app_id must be in range [0, 3]\nGiven: " << app_id);
    if (mode == FSRF::MODE::NONE)
        ERR("Mode must not be none");
    DBG("app_id: " << app_id);
    DBG("mode: " << mode_str(mode));
    DBG("batch_size: " << (void *)mmap_dma_size);

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

    next_free_page = phys_base >> 12;

    ASSERT(mmap_dma_size % 0x1000 == 0);

    // flush_tlb();
}

FSRF::~FSRF()
{
    abort = true;
    faultHandlerThread.join();
#ifdef PERF
    for (auto it = cumulative_times.begin(); it != cumulative_times.end(); it++)
    {
        std::cout << it->first << "_MS, " << it->second.count() * microseconds::period::num / microseconds::period::den << "\n";
    }
    for (auto it = num_calls.begin(); it != num_calls.end(); it++)
    {
        std::cout << it->first << "_CALLS, " << it->second << "\n";
    }
#endif
}

const char *FSRF::mode_str(MODE mode)
{
    constexpr const char *mode_string[4] = {"inv_read", "inv_write", "mmap", "managed"};
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

uint64_t FSRF::get_num_credits()
{
    return num_credits;
}

void *FSRF::fsrf_malloc_managed(uint64_t orig_length, uint64_t host_permissions, uint64_t device_permissions)
{
    const std::lock_guard<std::mutex> guard(lock);

    ASSERT(mode == MANAGED);
    uint64_t length = orig_length;
    DBG("Length " << (void *)length);
    if (length % mmap_dma_size != 0)
    {
        length += mmap_dma_size - (length % mmap_dma_size);
        DBG("Rounding up length to " << (void *)length);
    }

    START("MMAP");
    void *ptr = mmap(0, length + mmap_dma_size, host_permissions, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    END("MMAP");
    if (ptr == MAP_FAILED)
    {
        ERR("mmap failed");
    }

    DBG("mmap returned pointer: " << ptr);
    ASSERT(length % PAGE_SIZE == 0);
    ASSERT(length % mmap_dma_size == 0);

    uint64_t toReturn = (uint64_t)ptr;

    if (toReturn % mmap_dma_size != 0)
    {
        toReturn = toReturn + (mmap_dma_size - (toReturn % mmap_dma_size));
    }

    DBG("Final mmap range: " << (void *)toReturn << " - " << (void *)(((uint64_t)ptr) + length + mmap_dma_size));
    ASSERT(toReturn % mmap_dma_size == 0);
    ASSERT(toReturn + orig_length <= ((uint64_t)ptr) + length + mmap_dma_size);
    ASSERT(((uint64_t)toReturn + length) % mmap_dma_size == 0);
    ASSERT(toReturn >= (uint64_t)ptr);

    VME vme{toReturn, length, device_permissions, nullptr};
    vmes[toReturn] = vme;
    return (void *)toReturn;
}

void *FSRF::fsrf_malloc(uint64_t orig_length, uint64_t host_permissions, uint64_t device_permissions)
{
    const std::lock_guard<std::mutex> guard(lock);

    ASSERT(mode == MMAP);
    uint64_t length = orig_length;
    DBG("Length " << (void *)length);
    if (length % mmap_dma_size != 0)
    {
        length += mmap_dma_size - (length % mmap_dma_size);
        DBG("Rounding up length to " << (void *)length);
    }

    START("MMAP");
    void *ptr = mmap(0, length + mmap_dma_size, host_permissions, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    END("MMAP");
    if (ptr == MAP_FAILED)
    {
        ERR("mmap failed");
    }

    DBG("mmap returned pointer: " << ptr);
    ASSERT(length % PAGE_SIZE == 0);
    ASSERT(length % mmap_dma_size == 0);

    uint64_t toReturn = (uint64_t)ptr;

    if (toReturn % mmap_dma_size != 0)
    {
        toReturn = toReturn + (mmap_dma_size - (toReturn % mmap_dma_size));
    }

    DBG("Final mmap range: " << (void *)toReturn << " - " << (void *)(((uint64_t)ptr) + length + mmap_dma_size));
    ASSERT(toReturn % mmap_dma_size == 0);
    ASSERT(toReturn + orig_length <= ((uint64_t)ptr) + length + mmap_dma_size);
    ASSERT(((uint64_t)toReturn + length) % mmap_dma_size == 0);
    ASSERT(toReturn >= (uint64_t)ptr);

    VME vme{toReturn, length, device_permissions, nullptr};
    vmes[toReturn] = vme;

    uint64_t vpn = toReturn >> 12;
    for (uint64_t page = 0; page < length >> 12; ++page)
    {
        uint64_t device_ppn = allocate_device_ppn();
        // allocations guaranteed to be contiguous
        device_vpn_to_ppn[vpn + page] = device_ppn;
        write_tlb(vpn + page, device_ppn, /*writeable*/ true, true, true, false);
    }

    return (void *)toReturn;
}

// Sync entire VME containing addr to host 
void FSRF::sync_device_to_host(uint64_t *addr)
{
    ASSERT(mode == MMAP);
    const std::lock_guard<std::mutex> guard(lock);
    auto it = vmes.begin();
    while (it != vmes.end())
    {
        VME vme = it->second;
        // intervals overlap
        if (((uint64_t) addr >= vme.addr && (uint64_t) addr < (vme.addr + vme.size)))
        {
            DBG("VME addr: " << (void*) vme.addr << "\n");
            // unmap from addr to addr + size
            for (uint64_t vaddr = vme.addr; vaddr < vme.addr + vme.size; vaddr += mmap_dma_size)
            {
                ASSERT(vaddr % mmap_dma_size == 0);
                ASSERT(vme.size % mmap_dma_size == 0);

                ASSERT(device_vpn_to_ppn.find(vaddr >> 12) != device_vpn_to_ppn.end());

                // DBG("Reading " << (uint64_t *)vaddr << " from fpga to host");

                // dma from device to host
                ASSERT(device_vpn_to_ppn.find(vaddr >> 12) != device_vpn_to_ppn.end());
                fpga.dma_read((void *)vaddr, device_vpn_to_ppn[vaddr >> 12] << 12, mmap_dma_size);

                // DBG("Finished dma read");
            }
            return;
        }
        else
        {
            ++it;
        }
    }
    ERR("Invalid sync");
}

// Sync entire VME containing addr to device
void FSRF::sync_host_to_device(void *addr)
{
    ASSERT(mode == MMAP);
    const std::lock_guard<std::mutex> guard(lock);
    auto it = vmes.begin();
    while (it != vmes.end())
    {
        VME vme = it->second;
        if (((uint64_t)addr >= vme.addr && (uint64_t)addr < (vme.addr + vme.size)))
        {
            for (uint64_t vaddr = vme.addr; vaddr < vme.addr + vme.size; vaddr += mmap_dma_size)
            {
                ASSERT(vaddr % mmap_dma_size == 0);
                ASSERT(vme.size % mmap_dma_size == 0);

                DBG("Reading " << (uint64_t *)vaddr << " from host to device");

                // dma from device to host
                ASSERT(device_vpn_to_ppn.find(vaddr >> 12) != device_vpn_to_ppn.end());
                fpga.dma_write((void *)vaddr, device_vpn_to_ppn[vaddr >> 12] << 12, mmap_dma_size);

                DBG("Finished dma write");
            }
            return;
        }
        else
        {
            ++it;
        }
    }
    ERR("Invalid sync");
}

// brings back the dma batch size containing addr to the host
void FSRF::sync_managed(uint64_t *addr)
{
    DBG("In method");
    uint64_t vaddr = (uint64_t)addr;
    if (vaddr % mmap_dma_size != 0)
        vaddr -= vaddr % mmap_dma_size;
    ASSERT(mode == MANAGED);
    DBG("Getting VMEs");
    auto it = vmes.begin();
    DBG("Entering while loop");
    while (it != vmes.end())
    {
        VME vme = it->second;
        DBG("VME addr: " << (void *)vme.addr);
        // this is the right vme entry
        if (vaddr >= vme.addr && (vaddr < (vme.addr + vme.size)))
        {
            DBG("MANAGED mode bringing back " << (void *)addr << "\n");
            // make available on the host
            timed_mprotect((void *)addr, mmap_dma_size, PROT_READ | PROT_WRITE);

            // unmap from addr to vaddr + mmap_dma_size on device
            ASSERT(vaddr % mmap_dma_size == 0);
            ASSERT(vme.size % mmap_dma_size == 0);

            // this page was never put on the device
            if (device_vpn_to_ppn.find(vaddr >> 12) == device_vpn_to_ppn.end())
                continue;

            for (uint64_t curr = vaddr; curr < vaddr + mmap_dma_size; curr += 0x1000)
            {
                uint64_t vpn = curr >> 12;
                ASSERT(device_vpn_to_ppn.find(vpn) != device_vpn_to_ppn.end());
                write_tlb(vpn, device_vpn_to_ppn[vpn], false, false, false, false);
            }
            // timed_mprotect((void *)vaddr, mmap_dma_size, PROT_READ | PROT_WRITE);

            DBG("Reading " << (uint64_t *)vaddr << " from fpga to host");

            // dma from device to host
            ASSERT(device_vpn_to_ppn.find(vaddr >> 12) != device_vpn_to_ppn.end());
            fpga.dma_read((void *)vaddr, device_vpn_to_ppn[vaddr >> 12] << 12, mmap_dma_size);

            DBG("Finished dma read");

            for (uint64_t curr = vaddr; curr < vaddr + mmap_dma_size; curr += 0x1000)
            {
                uint64_t vpn = curr >> 12;
                // this page was never put on the device
                if (device_vpn_to_ppn.find(vpn) == device_vpn_to_ppn.end())
                    continue;
                // free up device page
                fsrf->free_device_vpn(vpn);
            }
            return;
        }
        else
        {
            ++it;
        }
    }
    ERR("Couldn't find vme entry");
}

void FSRF::fsrf_free(uint64_t *addr)
{
    ASSERT(mode == MMAP);
    auto it = vmes.begin();
    while (it != vmes.end())
    {
        VME vme = it->second;
        if ((uint64_t)addr >= vme.addr && (uint64_t)addr < (vme.addr + vme.size))
        {
            // unmap from addr to addr + size
            // if the user wanted this data written to host, they should have called msync.
            for (uint64_t vaddr = vme.addr; vaddr < vme.addr + vme.size; vaddr += 0x1000)
            {
                uint64_t vpn = vaddr >> 12;
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
    uint64_t toReturn = next_free_page;
    next_free_page += 1;

    if (next_free_page == phys_bound >> 12)
    {
        ERR("Too many pages allocated");
    }

    return toReturn;
}

void FSRF::free_device_vpn(uint64_t vpn)
{
    // allocated_device_ppns.erase(device_vpn_to_ppn[vpn]);
    ASSERT(device_vpn_to_ppn.find(vpn) != device_vpn_to_ppn.end());
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
    ASSERT(!huge);

    // Max of 36 vpn bits
    ASSERT(vpn < ((uint64_t)1 << 36));
    // Max of 24 ppn bits
    ASSERT(ppn < (1 << 24));

    uint64_t tlb_addr = dram_tlb_addr(vpn);
    uint64_t entry = (vpn << 28) | (ppn << 4) | (writeable << 2) | (readable << 1) | present;
    // DBG("Entry " << (void *)entry);

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
        // find a place to put the data
        uint64_t device_ppn = allocate_device_ppn();
        // put the data there
        fpga.dma_write((void *)vaddr, device_ppn << 12, bytes);
        // remember where we put it
        device_vpn_to_ppn[vpn] = device_ppn;

        // make it inaccessible on the host
        timed_mprotect((void *)vaddr, bytes, PROT_NONE);

        // create a tlb entry
        write_tlb(vpn, device_ppn, /*writeable*/ true, true, true, false);

        // respond to the fault
        respond_tlb(device_ppn, true);
    }
    else if (mode == MODE::INV_WRITE)
    {

        // If we are reading, we need to make it readable on the host and device
        if (read)
        {
            START("MPROTECT_RW_TO_R");
            timed_mprotect((void *)vaddr, bytes, PROT_READ);
            // it is already readonly on the host at this point
            END("MPROTECT_RW_TO_R");

            // find a place to put the data
            uint64_t device_ppn = allocate_device_ppn();
            // put the data there
            fpga.dma_write((void *)vaddr, device_ppn << 12, bytes);
            // remember where we put it
            device_vpn_to_ppn[vpn] = device_ppn;

            // create a tlb entry
            write_tlb(vpn, device_ppn, /*writeable*/ false, true, true, false);

            // respond to the fault
            respond_tlb(device_ppn, true);
        }
        // if it's a write, there are two cases.
        // either the page is already readable on the device, or this is a blind write.
        else
        {
            // the page is already on the device, we just need to invalidate it on the host
            // and make it writeable on the device
            if (device_vpn_to_ppn.find(vpn) != device_vpn_to_ppn.end())
            {
                timed_mprotect((void *)vaddr, bytes, PROT_NONE);
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
                timed_mprotect((void *)vaddr, bytes, PROT_NONE);

                // respond to the fault
                respond_tlb(device_ppn, true);
            }
        }
    }
    else if (mode == MMAP)
    {
        ERR("Device should not fault for MMAP mode");
    }
    else
    {
        ASSERT(mode == MANAGED);
        // and make it writeable on the device
        if (device_vpn_to_ppn.find(vpn) != device_vpn_to_ppn.end())
        {
            DBG("Data is already there!");
            std::cout << "data already here\n";
            respond_tlb(device_vpn_to_ppn[vpn], true);
            return;
        }

        uint64_t device_ppn = -1;
        VME vme;

        uint64_t fault_vpn = vaddr >> 12;

        // align vaddr / vpn to mmap_dma_size
        vaddr -= vaddr % mmap_dma_size;
        vpn = vaddr >> 12;

        DBG("Aligned vaddr: " << (void *)vaddr);

        for (auto it = vmes.begin(); it != vmes.end(); ++it)
        {
            vme = it->second;
            // This is a valid mmapped address
            if (vaddr >= vme.addr && vaddr < vme.addr + vme.size)
            {
                ASSERT(vme.addr % mmap_dma_size == 0);
                ASSERT(vme.size % mmap_dma_size == 0);
                DBG("Found suitable VME");
                device_ppn = allocate_device_ppn();
                device_vpn_to_ppn[vpn] = device_ppn;
                for (uint64_t page = 1; page < mmap_dma_size >> 12; ++page)
                {
                    // allocations guaranteed to be contiguous
#ifdef DEBUG
                    uint64_t next_ppn = allocate_device_ppn();
                    ASSERT(next_ppn == device_ppn + page);
#else
                    allocate_device_ppn();
#endif
                    device_vpn_to_ppn[vpn + page] = device_ppn + page;
                }

                // fpga.dma_write((void *)vaddr, device_ppn << 12, mmap_dma_size);

                // write_tlb(vpn, device_ppn, /*writeable*/ true, true, true, false);
                // respond_tlb(device_ppn, true);

                // for (uint64_t page = 1; page < mmap_dma_size >> 12; ++page)
                for (uint64_t page = 0; page < mmap_dma_size >> 12; ++page)
                {
                    ASSERT(device_vpn_to_ppn[vpn + page] == device_ppn + page);
                    write_tlb(vpn + page, device_ppn + page, /*writeable*/ true, true, true, false);
                }

                fpga.dma_write((void *)vaddr, device_ppn << 12, mmap_dma_size);
                timed_mprotect((void *)vaddr, mmap_dma_size, PROT_NONE);
                ASSERT(device_vpn_to_ppn.find(fault_vpn) != device_vpn_to_ppn.end());
                respond_tlb(device_vpn_to_ppn[fault_vpn], true);
                return;
            }
        }
        ERR("Invalid device access");
    }
}

void FSRF::device_fault_listener()
{
    DBG("Starting up");
    while (true)
    {

        uint64_t fault = read_tlb_fault();

        ASSERT(fault != (uint64_t)-1);
        if (!(fault & 1))
        {
            num_credits = fault >> 57;
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
    ASSERT(sig == SIGSEGV);
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
            fsrf->timed_mprotect((void *)vaddr, 1 << 12, PROT_READ | PROT_WRITE);

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

            START("MPROTECT_NONE_TO_R");
            fsrf->timed_mprotect((void *)vaddr, 1 << 12, PROT_READ);
            END("MPROTECT_NONE_TO_R");

            // dma from device to host
            // we have to dma because the device has written to this page
            fsrf->fpga.dma_read((void *)vaddr, fsrf->device_vpn_to_ppn[vpn] << 12, (uint64_t)1 << 12);

            DBG("Finished dma read");
        }
        else if (fsrf->mode == MODE::MANAGED)
        {
            DBG("About to call sync managed");
            fsrf->sync_managed((uint64_t *)vaddr);
            DBG("Returned from sync managed");
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

int FSRF::timed_mprotect(void *addr, size_t len, int prot)
{
    // START("MPROTECT");
    if (prot == PROT_NONE)
    {
        START("MPROTECT_NONE");
    }
    else if (prot == (PROT_READ | PROT_WRITE))
    {
        START("MPROTECT_NONE_TO_RW");
    }

    int res = mprotect(addr, len, prot);
    assert(res == 0);
    if (prot == PROT_NONE)
    {
        END("MPROTECT_NONE");
    }
    else if (prot == (PROT_READ | PROT_WRITE))
    {
        END("MPROTECT_NONE_TO_RW");
    }

    // END("MPROTECT");
    return res;
}
