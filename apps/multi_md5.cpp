#include <assert.h>
#include <errno.h>
#include <iostream>
#include "fsrf.h"

#include "arg_parse.h"
using namespace std::chrono;

int main(int argc, char *argv[])
{
    ArgParse argsparse(argc, argv);
    bool debug = argsparse.getVerbose();
    FSRF::MODE mode = argsparse.getMode();

    FSRF fsrf[4];
    for (int i = 0; i < 4; i++)
    {
        fsrf[i] = FSRF{i, mode};
    }

    uint64_t size = 0x4000;
    void *buf[4];

    switch (mode)
    {
    case FSRF::MODE::INV_READ:
    case FSRF::MODE::INV_WRITE:
        for (int i = 0; i < 4; i++)
            buf[i] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        break;
    case FSRF::MODE::MMAP:
        for (int i = 0; i < 4; i++)
            buf[i] = fsrf.fsrf_malloc(size, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
        break;
    default:
        std::cerr << "unexpected mode\n";
        exit(1);
    }

    if (buf == MAP_FAILED)
        printf("Oh dear, something went wrong with allocation: \n\t%s\n", strerror(errno));

    for (int i = 0; i < 4; i++)
    {
        fsrf[i].cntrlreg_write(0x10, (uint64_t)buf); // src_addr
        fsrf[i].cntrlreg_write(0x18, 8);             // rd_credits
        fsrf[i].cntrlreg_write(0x20, size / 64);     // num 64 byte words
    }

    if (debug)
        std::cout << "successfully wrote cntrlregs\n";

    uint64_t val = 0;
    while (val != size / 64)
    {
        val = fsrf[0].cntrlreg_read(0x28);
        val = std::min(val, fsrf[1].cntrlreg_read(0x28));
        val = std::min(val, fsrf[2].cntrlreg_read(0x28));
        val = std::min(val, fsrf[3].cntrlreg_read(0x28));
    }
    if (debug)
        std::cout << "\nfpga is done\n";

    if (debug)
    {
        for (int i = 0; i < 4; i++)
        {
            val = fsrf[i].cntrlreg_read(0x0);
            std::cout << "ab: " << val << "\n";
            val = fsrf[i].cntrlreg_read(0x8);
            std::cout << "cd: " << val << "\n";
        }
    }
    // end runs
    // important -> polls on completion register
    // util.finish_runs(aos, end, 0x28, true, configs[0].num_words);
    // important -> answer lives in cntrlreg_read 0x0, 0x8

    return 0;
}
