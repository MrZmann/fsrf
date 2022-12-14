#include <chrono>
#include <cstring>
#include <iostream>
#include "fpga.h"

using namespace std::chrono;

#ifdef DEBUG
#define DBG(x) std::cout << "[" << __FUNCTION__ << ":" << __LINE__ << "]\t" << x << std::endl
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
#define TRACK(name)                                                \
    {                                                              \
        cumulative_times[name] = std::chrono::nanoseconds::zero(); \
        num_calls[name] = 0;                                       \
    }

#define START(name)                                                    \
    {                                                                  \
        ASSERT(cumulative_times.find(name) != cumulative_times.end()); \
        last_start[name] = high_resolution_clock::now();               \
        num_calls[name] += 1;                                          \
    }

#define END(name)                                                      \
    {                                                                  \
        ASSERT(cumulative_times.find(name) != cumulative_times.end()); \
        ASSERT(last_start.find(name) != last_start.end());             \
        auto end = high_resolution_clock::now();                       \
        cumulative_times[name] += end - last_start[name];              \
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

FPGA::FPGA(uint64_t slot, uint64_t app_id, uint64_t base_tlb_addr) : app_id(app_id)
{
    TRACK("APP_REG");
    TRACK("SYS_REG");
    TRACK("MEM_REG");
    TRACK("DMA_READ");
    TRACK("DMA_WRITE");
    TRACK("ATTACH_PCI");
    TRACK("HUGE_PAGE");
    TRACK("ZERO_TLB");
    //TRACK("DMA_READ_MEMCPY");

    int rc, fd;
    int xfer_buf_size = 2 << 20;
    // char xdma_str[19];

    // Init FPGA library
    rc = fpga_mgmt_init();

    fail_on(rc, out, "Unable to initialize the fpga_mgmt library\n");

    START("ATTACH_PCI");

    // Attach PCIe BARs
    rc |= fpga_pci_attach(slot, FPGA_APP_PF, APP_PF_BAR0, 0, &app_bar_handle);
    rc |= fpga_pci_attach(slot, FPGA_APP_PF, APP_PF_BAR1, 0, &sys_bar_handle);
    rc |= fpga_pci_attach(slot, FPGA_APP_PF, APP_PF_BAR4, BURST_CAPABLE, &mem_bar_handle);
    fail_on(rc, out, "Unable to attach PCIe BAR(s)\n");
    END("ATTACH_PCI");

    read_sys_reg(9, app_id * 8, pages_xfered);

    fd = open("/proc/sys/vm/nr_hugepages", O_WRONLY);
    pwrite(fd, "4\n", 3, 0);
    close(fd);

    START("HUGE_PAGE");

    xfer_buf = ::mmap(NULL, xfer_buf_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (xfer_buf == MAP_FAILED)
    {
        perror("xfer_buf allocation error");
        printf("errno: %d\n", errno);
        exit(EXIT_FAILURE);
    }
    if (mlock(xfer_buf, xfer_buf_size))
    {
        perror("mlock error");
    }

    phys_buf = virt_to_phys((uint64_t)xfer_buf);
    for (uint64_t i = 0; i < 512; ++i)
    {
        uint64_t vpn = ((uint64_t)xfer_buf) + 4096 * i;
        uint64_t tppn = virt_to_phys(vpn);
        uint64_t pppn = phys_buf + 4096 * i;
        if (tppn != pppn)
        {
            printf("DMA buffer not contiguous, vpn %lu -> %lu, ppn %lu -> %lu\n", (uint64_t)xfer_buf, vpn, phys_buf, tppn);
            exit(EXIT_FAILURE);
        }
    }
    END("HUGE_PAGE");

    // zero out TLB
    START("ZERO_TLB");
    for (uint64_t ppn = base_tlb_addr; ppn <= 32768; ppn += 512)
    {
        dma_wrapper(false, 512, ppn, app_id);
    }
    END("ZERO_TLB");
out:
    return;
}

FPGA::~FPGA()
{
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

int FPGA::read_app_reg(uint64_t app_id, uint64_t addr, uint64_t &value)
{
    START("APP_REG");
    int res = reg_access(app_bar_handle, app_id, addr, value, false, true);
    END("APP_REG");
    return res;
}

int FPGA::write_app_reg(uint64_t app_id, uint64_t addr, uint64_t value)
{
    START("APP_REG");
    int res = reg_access(app_bar_handle, app_id, addr, value, true, true);
    END("APP_REG");
    return res;
}

int FPGA::read_sys_reg(uint64_t app_id, uint64_t addr, uint64_t &value)
{
    START("SYS_REG");
    int res = reg_access(sys_bar_handle, app_id, addr, value, false, true);
    END("SYS_REG");
    return res;
}

int FPGA::write_sys_reg(uint64_t app_id, uint64_t addr, uint64_t value)
{
    START("SYS_REG");
    int res = reg_access(sys_bar_handle, app_id, addr, value, true, true);
    END("SYS_REG");
    return res;
}

int FPGA::read_mem_reg(uint64_t addr, uint64_t &value)
{
    START("MEM_REG");
    int res = reg_access(mem_bar_handle, 0, addr, value, false, false);
    END("MEM_REG");
    return res;
}

int FPGA::write_mem_reg(uint64_t addr, uint64_t value)
{
    START("MEM_REG");
    int res = reg_access(mem_bar_handle, 0, addr, value, true, false);
    END("MEM_REG");
    return res;
}

uint64_t FPGA::virt_to_phys(uint64_t virt_addr)
{
    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd == -1)
    {
        perror("pagemap open error");
        exit(EXIT_FAILURE);
    }
    uint64_t pfn = 0;
    uint64_t offset = virt_addr / getpagesize() * 8;
    if (pread(fd, &pfn, 8, offset) != 8)
    {
        perror("virt_to_phys error");
        exit(EXIT_FAILURE);
    }
    close(fd);
    pfn &= 0x7FFFFFFFFFFFFF;
    return (pfn << 12);
}

int FPGA::dma_read(void *buf, uint64_t addr, uint64_t bytes)
{
    START("DMA_READ");
    ASSERT(addr % 0x1000 == 0);
    ASSERT(bytes % 0x1000 == 0);
    uint64_t num_pages = bytes / 0x1000;
    dma_wrapper(true, num_pages, addr / 0x1000, app_id);
    //START("DMA_READ_MEMCPY");
    std::memcpy(buf, xfer_buf, bytes);
    //END("DMA_READ_MEMCPY");
    std::memset(xfer_buf, 0, bytes);
    END("DMA_READ");
    return 0;
}

int FPGA::dma_write(void *buf, uint64_t addr, uint64_t bytes)
{
    START("DMA_WRITE");
    ASSERT(addr % 0x1000 == 0);
    ASSERT(bytes % 0x1000 == 0);
    uint64_t num_pages = bytes / 0x1000;
    std::memcpy(xfer_buf, buf, bytes);
    dma_wrapper(false, num_pages, addr / 0x1000, app_id);
    std::memset(xfer_buf, 0, bytes);
    END("DMA_WRITE");
    return 0;
}

void FPGA::dma_wrapper(bool from_device, uint64_t num_pages, uint64_t ppn, uint64_t app_id)
{
    ASSERT(num_pages <= 512);

    if (send_data)
    {
        if (pcim)
        {
            uint64_t pcie_addr = phys_buf >> 12;
            uint64_t fpga_addr = ppn;
            uint64_t count = num_pages - 1;
            uint64_t channel = app_id;
            uint64_t fpga_read = from_device;

            uint64_t command = pcie_addr | (fpga_addr << 28) | (count << 52) | (channel << 61) | (fpga_read << 63);
            // printf("pcim %lu: %lu %lu %lu %lu %lu -> %lu\n", app_id, pcie_addr, fpga_addr, count, channel, fpga_read, command);
            write_sys_reg(9, 0, command);

            uint64_t pages_done = pages_xfered + num_pages;
            do
            {
                read_sys_reg(9, app_id * 8, pages_xfered);
                // printf("pcim %lu: %lu pages xfered / %lu pages done, %lu pages left\n", app_id, pages_xfered, pages_done, pages_done - pages_xfered);
                if (pages_xfered > pages_done)
                {
                    printf("pcim accounting error\n");
                    exit(EXIT_FAILURE);
                }
                // usleep(1000000);
            } while (pages_xfered < pages_done);
        }
        else
        {
            if (from_device)
            {
                if (dma_read(xfer_buf, ppn << 12, num_pages << 12))
                {
                    printf("Transfer metadata: app %lu, buf %p, ppn %lu, pages %lu\n",
                           app_id, xfer_buf, ppn, num_pages);
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                if (dma_write(xfer_buf, ppn << 12, num_pages << 12))
                {
                    printf("Transfer metadata: app %lu, buf %p, ppn %lu, pages %lu\n",
                           app_id, xfer_buf, ppn, num_pages);
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
}

int FPGA::reg_access(pci_bar_handle_t &bar_handle, uint64_t app_id, uint64_t addr,
                     uint64_t &value, bool write, bool mask)
{
    int rc;

    // Check the address is 64-bit aligned
    fail_on((addr % 8), out, "Addr is not correctly aligned\n");

    // Update addr with mask
    if (mask)
    {
        addr = (addr >> 3) << 7;
        app_id = app_id << 3;
        addr = addr | app_id;
    }

    // Do access
    if (write)
    {
        rc = fpga_pci_poke64(bar_handle, addr, value);
    }
    else
    {
        rc = fpga_pci_peek64(bar_handle, addr, &value);
    }
    fail_on(rc, out, "Unable to access bar");

    return rc;
out:
    return 1;
}
