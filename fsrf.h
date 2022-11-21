#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <signal.h>
#include <stdint.h>
#include <thread>
#include <unordered_map>

#include "fpga.h"

class FSRF;
extern FSRF *fsrf;

class FSRF
{
public:
    enum MODE
    {
        NONE = -1,
        INV_READ = 0,
        INV_WRITE = 1,
        MMAP = 2
    };

    FSRF(uint64_t app_id, MODE mode, bool debug, int batch_size);
    ~FSRF();

    static const char *mode_str(MODE mode);

    void cntrlreg_write(uint64_t addr, uint64_t value);
    uint64_t cntrlreg_read(uint64_t addr);

    uint64_t get_num_credits();
    /*
    flags -
    permissions on fpga
    single use
    random access vs sequential
    */

    void *fsrf_malloc(uint64_t length, uint64_t host_permissions, uint64_t device_permissions);
    void sync_device_to_host(uint64_t *addr, size_t length);

    void fsrf_free(uint64_t *addr);

private:
    bool debug;
    bool abort = false;
    MODE mode;

    // device paging
    std::unordered_map<uint64_t, uint64_t>
        device_vpn_to_ppn;
    uint64_t phys_base;  // lowest paddr for app_id
    uint64_t phys_bound; // highest paddr for app_id
    uint64_t next_free_page;

    // device
    FPGA fpga;
    uint64_t num_credits;

    std::mutex lock;

    // host info
    std::thread faultHandlerThread;
    uint64_t app_id;

    // mmap info
    struct VME
    {
        uint64_t addr;
        uint64_t size;
        uint64_t prot;

        VME *next;
    } typedef VME;

    std::map<uint64_t, VME> vmes;

    uint64_t mmap_dma_size;

    std::unordered_map<std::string, std::chrono::duration<int64_t, std::nano>> cumulative_times;
    std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> last_start;

    void
    respond_tlb(uint64_t ppn, uint64_t valid);
    uint64_t allocate_device_ppn();
    void free_device_vpn(uint64_t vpn);
    uint64_t read_tlb_fault();
    uint64_t dram_tlb_addr(uint64_t vpn);

    void flush_tlb();
    void write_tlb(uint64_t vpn,
                   uint64_t ppn,
                   uint64_t writeable,
                   uint64_t readable,
                   uint64_t present,
                   uint64_t huge);

    void evict_tlb();

    void dmaWrite();
    void dmaRead();

    bool should_handle_fault(uint64_t fault);
    void handle_device_fault(bool read, uint64_t vpn);
    void device_fault_listener();

    static void handle_host_fault(int sig, siginfo_t *info, void *ucontext);

    int timed_mprotect(void *addr, size_t len, int prot);
};
