#include <iostream>
#include <stdio.h>
#include "arg_parse.h"
#include "fsrf.h"

int main(int argc, char *argv[])
{

    ArgParse argsparse(argc, argv);
    FSRF::MODE mode = argsparse.getMode();
    bool debug = argsparse.getVerbose();

    FSRF fsrf{argsparse.getAppId(), mode, debug};
    void *src;
    void *dest;

    int size = 0x2000;
    switch (mode)
    {
    case FSRF::MODE::INV_READ:
    case FSRF::MODE::INV_WRITE:
        src = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        dest = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        break;
    case FSRF::MODE::MMAP:
        src = fsrf.fsrf_malloc(size, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
        dest = fsrf.fsrf_malloc(size, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
        break;
    default:
        std::cerr << "unexpected mode\n";
        exit(1);
    }

    // start runs

    // junk
    fsrf.cntrlreg_write(0x00, 1);
    fsrf.cntrlreg_write(0x08, 2);
    fsrf.cntrlreg_write(0x10, 3);
    fsrf.cntrlreg_write(0x18, 4);

    fsrf.cntrlreg_write(0x20, (uint64_t) src);  // where to read from
    fsrf.cntrlreg_write(0x28, (uint64_t) dest); // where to write to
    fsrf.cntrlreg_write(0x38, 8);    // read credits
    fsrf.cntrlreg_write(0x40, 8);    // write credits

    // write this last because it starts the app
    fsrf.cntrlreg_write(0x30, size / 64); // num words

    uint64_t val = 1;
    while (val != 0)
    {
        val = fsrf.cntrlreg_read(0x38);
    }

    return 0;
}
