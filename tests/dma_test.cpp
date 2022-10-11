#include <iostream>
#include <memory>
#include <thread>

#include "fpga.h"

int main(int argc, char **argv)
{
    FPGA fpga{0, 0};
    uint64_t val = 0;
    fpga.write_sys_reg(0, 0, val);
    fpga.write_sys_reg(0, 0, val);

    int* buf[0x1000];
    for(int i = 0; i < 0x1000; i++){
        buf[i] = i;
    }

    fpga.dma_write((void*) buf, 0xf000, sizeof(int) * 0x1000);
    
    int* read[0x1000];
    fpga.dma_read((void*) read, 0xf000, sizeof(int) * 0x1000);

    for(int i = 0; i < 0x1000; i++){
        assert(read[i] == i);
    }
    

    return 0;
}
