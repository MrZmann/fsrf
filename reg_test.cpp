#include <iostream>
#include <memory>
#include <thread>

#include "fpga.h"

int main(int argc, char **argv)
{
    FPGA fpga = new FPGA(0);
    fpga.write_sys_reg(0, 0);
    fpga.write_sys_reg(0, 0);

    return 0;
}