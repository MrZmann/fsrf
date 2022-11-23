#include <iostream>
#include <memory>
#include <thread>

#include "fpga.h"

int main(int argc, char **argv)
{
    uint64_t vpn = 0;
    // these determine the set
    const uint64_t app_offsets[4] = {0, 32ull << 30, 16ull << 30, 48ull << 30};
    const uint64_t tlb_bits = 21;
    uint64_t vpn_index = vpn & ((1 << tlb_bits) - 1);

    // this determines the way
    uint64_t vpn_offset = (vpn >> tlb_bits) & 0x7;
    uint64_t dram_addr = vpn_index * 64 + app_offsets[0] + vpn_offset * 8;


    FPGA fpga{0, 0, dram_addr};
    uint64_t val = 0;

    fpga.write_sys_reg(0, 0, val);
    fpga.write_sys_reg(0, 0, val);

    return 0;
}
