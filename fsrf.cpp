#include <assert.h>
#include <iostream>
#include <sys/mman.h>

#include "fsrf.h"

#define DBG(x) \
    if (debug) \
    std::cout << "[" << __FUNCTION__ << ":" << __LINE__ << "]\t" << x << std::endl
#define ERR(x)                                                                      \
{std::cerr << "[" << __FUNCTION__ << ":" << __LINE__ << "]\t" << x << std::endl; exit(1);}
#define PAGE_SIZE 0x1000
// Global instance for the SIGSEGV handler to use
FSRF *fsrf = nullptr;

FSRF::FSRF(uint64_t app_id, MODE mode) : 
                              mode(mode),
                              fpga(0, app_id),
                              app_id(app_id)
{
    DBG("app_id: " << app_id);
    DBG("mode: " << mode_str[mode]);

    if (fsrf != nullptr)
    {
        ERR("Global fsrf object already initialized");
    }

    fsrf = this;
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
    phys_base = addrs[app_id];
    phys_bound = addrs[app_id] + (16 << 20) / max_apps;

    flush_tlb();
}

FSRF::~FSRF()
{
    abort = true;
    faultHandlerThread.join();
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

void *FSRF::fsrf_mmap(void *addr_hint, size_t length, int prot, int flags, int fd, off_t offset)
{
    assert(mode == MMAP);
    void *res = mmap(addr_hint, length, prot, flags, fd, offset);
    DBG("hola");
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
    VME vme{addr, size, prot, nullptr};
    vmes[addr] = vme;
    return res;
}

int FSRF::fsrf_munmap(void *addr, size_t length)
{
    assert(mode == MMAP);
    uint64_t low = (uint64_t)addr;
    uint64_t high = low + (uint64_t)length;
    auto it = vmes.begin();
    while (it != vmes.end())
    {
        VME vme = it->second;
        // intervals overlap
        if ((low > vme.addr && low < (vme.addr + vme.size)) ||
            (high > vme.addr && high < (vme.addr + vme.size)))
        {
            // unmap from addr to addr + size
            // if the user wanted this data written to host, they should have called msync.
            for (uint64_t vpn = vme.addr; vpn < vme.addr + vme.size; ++vpn)
            {
                fsrf->free_device_vpn(vpn);
            }
            it = vmes.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return 0;
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
                     bool writeable,
                     bool readable,
                     bool present,
                     bool huge)
{
    assert(!huge);

    // Max of 36 vpn bits
    assert(vpn < ((uint64_t)1 << 36));
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

    DBG("Handling device fault at: " << (uint64_t *)vaddr);

    // TODO: Lock so that only device or host fault handler can race
    // TODO: check return value
    mprotect((void *)vaddr, bytes, PROT_READ);

    uint64_t device_ppn;
    if (mode == MODE::INV_READ ||
        // If we are invalidating on write, we need to check if a page is already on the device
        (mode == MODE::INV_WRITE && device_vpn_to_ppn.find(vpn) == device_vpn_to_ppn.end()))
    {
        device_ppn = allocate_device_ppn();
        fpga.dma_write((void *)vaddr, device_ppn << 12, bytes);

        DBG("Finished dma write");
    }
    else if (mode == MODE::INV_WRITE)
    {
        // the data already exists readonly on the host, so no dma necessary
        device_ppn = device_vpn_to_ppn[vpn];
    }
    else
    {
        bool valid_addr = false;
        for (auto it = vmes.begin(); it != vmes.end(); ++it)
        {
            VME vme = it->second;
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
        if (!valid_addr) ERR("Invalid device access");
    }

    DBG("Device ppn: " << device_ppn);
    device_vpn_to_ppn[vpn] = device_ppn;

    if (mode == MODE::INV_READ ||
        (mode == MODE::INV_WRITE && !read))
    {
        mprotect((void *)vaddr, bytes, PROT_NONE);
        write_tlb(vpn, device_ppn, true, /*writeable*/ true, true, false);
        respond_tlb(device_ppn, true);
    }
    else if (mode == MODE::INV_WRITE)
    {
        write_tlb(vpn, device_ppn, true, /*writeable*/ false, true, false);
        respond_tlb(device_ppn, true);
    }
    else
    {
        // both host and device can read / write
        // TODO actually use VME permissions
        mprotect((void *)vaddr, bytes, PROT_READ | PROT_WRITE);
        write_tlb(vpn, device_ppn, true, /*writeable*/ true, true, false);
        respond_tlb(device_ppn, true);
    }
}

void FSRF::device_fault_listener()
{
    DBG("Starting up");
    while (true)
    {
        if (abort)
            return;

        uint64_t fault = read_tlb_fault();
        if (!(fault & 1) || fault == (uint64_t)-1)
            continue;
        DBG("Found fault");

        bool read = fault & 0x2;
        uint64_t vpn = (fault >> 2) & 0xFFFFFFFFFFFFF;

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
