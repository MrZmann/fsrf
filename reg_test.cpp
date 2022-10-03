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

    return 0;
}
