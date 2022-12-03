#include "arg_parse.h"
#include "Bench.h"

class Aes : public Bench
{
    const int size = 1073741824 / 2;
    void *src;
    void *dest;

public:
    Aes(ArgParse argparse) : Bench(argparse) {}

    virtual void setup()
    {
        switch (mode)
        {
        case FSRF::MODE::INV_READ:
        case FSRF::MODE::INV_WRITE:
            src = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            dest = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            break;
        case FSRF::MODE::MMAP:
            src = fsrf->fsrf_malloc(size, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
            fsrf->sync_host_to_device(src);
            dest = fsrf->fsrf_malloc(size, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
            break;
        case FSRF::MODE::MANAGED:
            src = fsrf->fsrf_malloc_managed(size, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
            dest = fsrf->fsrf_malloc_managed(size, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
            break;
        default:
            std::cerr << "unexpected mode\n";
            exit(1);
        }

        if (src == MAP_FAILED || dest == MAP_FAILED)
        {
            std::cerr << "allocation failed\n";
            exit(1);
        }
    }

    virtual void start_fpga()
    {
        fsrf->cntrlreg_write(0x00, 1);
        fsrf->cntrlreg_write(0x08, 2);
        fsrf->cntrlreg_write(0x10, 3);
        fsrf->cntrlreg_write(0x18, 4);

        fsrf->cntrlreg_write(0x20, (uint64_t)src);  // where to read from
        fsrf->cntrlreg_write(0x28, (uint64_t)dest); // where to write to
        fsrf->cntrlreg_write(0x38, 8);              // read credits
        fsrf->cntrlreg_write(0x40, 8);              // write credits

        // write this last because it starts the app
        fsrf->cntrlreg_write(0x30, size / 64); // num words
    }

    virtual void wait_for_fpga()
    {
        uint64_t val = 1;
        while (val != 0)
        {
            val = fsrf->cntrlreg_read(0x38);
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

        uint64_t *output = (uint64_t *)dest;
        uint64_t output_sum = 0;

        if (mode == FSRF::MODE::MMAP)
        {
            fsrf->sync_device_to_host(output, size);
        }

        for (uint64_t i = 0; i < size / sizeof(uint64_t); i += 0x1000 / sizeof(uint64_t))
        {
            output_sum += output[i];
        }
        if (verbose)
            std::cout << "out sum: " << output_sum << "\n";

        // assert(output_sum == 8785770774558841555);
        assert(output_sum == 7075813084211175919);

        return;
    }
};
