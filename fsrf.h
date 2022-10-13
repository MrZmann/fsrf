#pragma once

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
    FSRF(uint64_t app_id);
    void cntrlreg_write(uint64_t addr, uint64_t value);
    uint64_t cntrlreg_read(uint64_t addr);

private:
    // device paging
    std::unordered_map<uint64_t, uint64_t> device_vpn_to_ppn;
    std::set<uint64_t> allocated_device_ppns;
    uint64_t phys_base;  // lowest paddr for app_id
    uint64_t phys_bound; // highest paddr for app_id

    // device
    FPGA fpga;

    // host info
    std::thread faultHandlerThread;
    uint64_t app_id;

    // uint64_t host_vpn_to_ppn(uint64_t ppn);
    void respond_tlb(uint64_t ppn, uint64_t valid);
    uint64_t allocate_device_ppn();
    void free_device_vpn(uint64_t vpn);
    uint64_t read_tlb_fault();
    uint64_t dram_tlb_addr(uint64_t vpn);

    void flush_tlb();
    void write_tlb(uint64_t vpn,
                   uint64_t ppn,
                   bool writeable,
                   bool readable,
                   bool present,
                   bool huge);

    void evict_tlb();

    void dmaWrite();
    void dmaRead();

    bool should_handle_fault(uint64_t fault);
    void handle_device_fault(bool read, uint64_t vpn);
    void device_fault_listener();

    static void handle_host_fault(int sig, siginfo_t *info, void *ucontext);
};
