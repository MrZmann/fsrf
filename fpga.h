#include <assert.h>
#include <chrono>
#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include <fpga_pci.h>
#include <fpga_mgmt.h>
#include <utils/lcd.h>
#include <utils/sh_dpi_tasks.h>

const bool file_io = true;
const bool send_data = true;
const bool metrics = true;
const bool tracing = false;
const bool pcim = true;
const uint64_t max_apps = 4;

class FPGA
{
    const bool pcis = false;
    uint64_t app_id;

public:
    FPGA(int slot, int app_id);

    int read_app_reg(uint64_t app_id, uint64_t addr, uint64_t &value);
    int write_app_reg(uint64_t app_id, uint64_t addr, uint64_t &value);

    int read_sys_reg(uint64_t app_id, uint64_t addr, uint64_t &value);
    int write_sys_reg(uint64_t app_id, uint64_t addr, uint64_t &value);

    int read_mem_reg(uint64_t addr, uint64_t &value);
    int write_mem_reg(uint64_t addr, uint64_t value);

    uint64_t virt_to_phys(uint64_t virt_addr);

    int dma_read(void *buf, uint64_t addr, uint64_t bytes);
    int dma_write(void *buf, uint64_t addr, uint64_t bytes);

    // data management
    void *xfer_buf;
    uint64_t phys_buf;
    uint64_t pages_xfered;

    void dma_wrapper(bool from_device, uint64_t num_pages, uint64_t ppn, uint64_t app_id);

private:
    // PCIe IDs
    const static uint16_t pci_vendor_id = 0x1D0F; /* PCI Vendor ID */
    const static uint16_t pci_device_id = 0xF001; /* PCI Device ID */

    // BARs
    pci_bar_handle_t app_bar_handle;
    pci_bar_handle_t sys_bar_handle;
    pci_bar_handle_t mem_bar_handle;

    // XDMA fds
    int dth_fd[4];
    int htd_fd[4];

    int reg_access(pci_bar_handle_t &bar_handle, uint64_t app_id, uint64_t addr,
                   uint64_t &value, bool write, bool mask);
};
