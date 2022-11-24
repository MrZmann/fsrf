#include "arg_parse.h"
#include "Bench.h"

class Md5 : public Bench
{
    const uint64_t size = 1073741824;
    void *buf;

public:
    Md5(ArgParse argparse) : Bench(argparse) {}

    virtual void setup()
    {
        switch (mode)
        {
        case FSRF::MODE::INV_READ:
        case FSRF::MODE::INV_WRITE:
            buf = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            break;
        case FSRF::MODE::MMAP:
            buf = fsrf->fsrf_malloc(size, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
            break;
        default:
            std::cerr << "unexpected mode\n";
            exit(1);
        }

        if (buf == MAP_FAILED)
            printf("Oh dear, something went wrong with mmap: \n\t%s\n", strerror(errno));
    }

    virtual void start_fpga()
    {
        fsrf->cntrlreg_write(0x10, (uint64_t)buf); // src_addr
        fsrf->cntrlreg_write(0x18, 8);             // rd_credits
        fsrf->cntrlreg_write(0x20, size / 64);     // num 64 byte words
    }

    virtual void wait_for_fpga()
    {
        uint64_t val = 0;
        while (val != size / 64)
        {
            val = fsrf->cntrlreg_read(0x28);
        }
    }

    virtual void copy_back_output()
    {
        uint64_t ab = fsrf->cntrlreg_read(0x0);
        assert(ab == 5116089179561787392); 
        uint64_t cd = fsrf->cntrlreg_read(0x8);
        assert(cd == 1945555042127839232);
        if (true || verbose)
        {
            std::cout << "ab: " << ab << "\n";
            std::cout << "cd: " << cd << "\n";
        }
    }
};
