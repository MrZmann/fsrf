#include <set>
#include <stdint.h>
#include <thread>
#include <unordered_map>

#include "fpga.h"

class FSRF;
extern FSRF *fsrf;

class FSRF
{
    FSRF(uint64_t app_id) : use_dram_tlb(true), fpga(0, app_id), app_id(app_id)
    {
        // Global instance for the SIGSEGV handler to use
        fsrf = this;
        faultHandlerThread = std::thread(&FSRF::fault_listener, this);
        struct sigaction act = {0};
        act.sa_sigaction = FSRF::host_fault_handler;
        act.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &act, NULL);

        uint64_t addrs[4] = {0, 8 << 20, 4 << 20, 12 << 20};
        phys_base = addrs[app_id];
        phys_bound = addrs[app_id] + (16 << 20) / max_apps;
    }

    void cntrlreg_write(uint64_t addr, uint64_t value);
    uint64_t cntrlreg_read(uint64_t addr);

private:
    // device paging
    std::unordered_map<uint64_t, uint64_t> device_vpn_to_ppn;
    std::set<uint64_t> allocated_device_ppns;
    uint64_t phys_base;  // lowest paddr for app_id
    uint64_t phys_bound; // highest paddr for app_id

    // device configuration
    bool use_dram_tlb;

    // device
    FPGA fpga;

    // host info
    std::thread faultHandlerThread;
    uint64_t app_id;

    // uint64_t host_vpn_to_ppn(uint64_t ppn);
    uint64_t allocate_device_ppn();
    uint64_t read_tlb_fault();
    uint64_t dram_tlb_addr(uint64_t vpn);
    void write_tlb(uint64_t vpn,
                   uint64_t ppn,
                   bool writeable,
                   bool readable,
                   bool present,
                   bool huge);

    void evict_tlb();

    void dmaWrite();
    void dmaRead();

    bool FSRF::should_handle_fault(uint64_t fault);
    void FSRF::handle_fault(bool read, uint64_t vpn);
    void fault_listener();

    static void host_fault_handler(int sig, siginfo_t *info, void *ucontext);
};