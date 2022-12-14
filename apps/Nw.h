#include "arg_parse.h"
#include "Bench.h"

class Nw : public Bench
{

    const uint64_t s_ratio = 512 / 128;
    const uint64_t sc_ratio = 512 / 8;

    /*
    uint64_t length0 = 10;
    uint64_t length1 = 11;
    */

    uint64_t length0 = 13;
    uint64_t length1 = 14;

    uint64_t s0_addr;
    uint64_t s0_words;
    uint64_t s1_addr;
    uint64_t s1_words;
    uint64_t s1_credit;
    uint64_t sc_count;
    uint64_t sc_addr;
    uint64_t sc_words;
    uint64_t scd_addr;
    uint64_t scd_words;

public:
    Nw(ArgParse argparse) : Bench(argparse)
    {
    }

    virtual void setup()
    {
        s0_words = 1 << length0;
        s1_words = 1 << length1;
        s1_credit = 256;
        sc_count = (s0_words * s_ratio) * (s1_words * s_ratio);
        sc_words = (sc_count + sc_ratio - 1) / sc_ratio;

        int flags = MAP_PRIVATE | MAP_ANONYMOUS;
        uint64_t offset = 0;

        length0 = s0_words * 64;

        void *addr;

        if (mode == FSRF::MODE::MMAP)
        {
            addr = fsrf->fsrf_malloc(length0, PROT_READ, flags);
        }
        else if (mode == FSRF::MODE::MANAGED)
        {
            addr = fsrf->fsrf_malloc_managed(length0, PROT_READ, flags);
        }
        else
        {
            addr = mmap(0, length0, PROT_READ, flags, -1, 0);
        }

        if (addr == MAP_FAILED)
        {
            std::cerr << "Map failed\n";
            exit(1);
        }

        s0_addr = (uint64_t)addr;
        offset += length0;

        length1 = s1_words * 64;

        if (mode == FSRF::MODE::MMAP)
        {
            addr = fsrf->fsrf_malloc(length1, PROT_READ, flags);
        }
        else if (mode == FSRF::MODE::MANAGED)
        {
            addr = fsrf->fsrf_malloc_managed(length1, PROT_READ, flags);
        }
        else
        {
            addr = mmap(0, length1, PROT_READ, flags, -1, 0);
        }
        if (addr == MAP_FAILED)
        {
            std::cerr << "Map failed\n";
            exit(1);
        }

        s1_addr = (uint64_t)addr;
        offset += length1;

        length1 = sc_words * 64;

        if (mode == FSRF::MODE::MMAP)
        {
            addr = fsrf->fsrf_malloc(length1, PROT_WRITE, flags);
        }
        else if (mode == FSRF::MODE::MANAGED)
        {
            addr = fsrf->fsrf_malloc_managed(length1, PROT_WRITE, flags);
        }
        else
        {
            addr = mmap(0, length1, PROT_WRITE, flags, -1, 0);
        }

        if (addr == MAP_FAILED)
        {
            std::cerr << "Map failed\n";
            exit(1);
        }

        sc_addr = (uint64_t)addr;
    }

    virtual void wait_for_fpga()
    {
        if (mode == FSRF::MODE::MMAP)
        {
            frsf->sync_host_to_device(s0_addr);
            frsf->sync_host_to_device(s1_addr);
        }
        fsrf->cntrlreg_write(0x00, s0_addr);
        fsrf->cntrlreg_write(0x08, s0_words);
        fsrf->cntrlreg_write(0x10, s1_addr);
        fsrf->cntrlreg_write(0x18, s1_words);
        fsrf->cntrlreg_write(0x20, s1_credit);
        fsrf->cntrlreg_write(0x28, sc_count);
        fsrf->cntrlreg_write(0x30, sc_addr);
        fsrf->cntrlreg_write(0x38, sc_words);

        uint64_t val = 1;
        while (val != 0)
        {
            val = fsrf->cntrlreg_read(0x48);
        }
        uint64_t num_credits = 1;

        while (num_credits)
        {
            num_credits = fsrf->get_num_credits();
            // std::cout << "Waiting " << num_credits << "\n";
        }
    }

    virtual void copy_back_output()
    {
        uint64_t *output = (uint64_t *)sc_addr;
        uint64_t output_sum = 0;

        if (mode == FSRF::MODE::MMAP)
        {
            fsrf->sync_device_to_host(output);
        }

        for (uint64_t i = 0; i < length1 / sizeof(uint64_t); i += 0x1000 / sizeof(uint64_t))
        {
            output_sum += output[i];
        }
        if (verbose)
            std::cout << "out sum: " << output_sum << "\n";
    }
};
