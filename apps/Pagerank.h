#include "arg_parse.h"
#include "Bench.h"

class Pagerank : public Bench
{

    uint64_t num_verts;
    uint64_t num_edges;
    uint64_t vert_ptr;
    uint64_t edge_ptr;
    uint64_t input_ptr;
    uint64_t output_ptr;
    void *read_ptr;
    void *write_ptr;

    num_verts = 1000448;
    num_edges = 3105792;
    uint64_t read_length, write_length;

public:
    Pagerank(ArgParse argparse) : Bench(argparse) {}

    virtual void setup()
    {
        read_length = num_verts * (16 + 8) + num_edges * 8;
        write_length = num_verts * 8;

        int fd = open("/home/centos/fsrf/inputs/webbase-1M/webbase-1M.bin", O_RDWR);
        assert(fd != -1);

        if (mode == FSRF::MODE::INV_READ || mode == FSRF::MODE::INV_WRITE)
        {
            read_ptr = mmap(0, read_length, PROT_READ, MAP_SHARED, fd, 0);
            // limitation - transparent version doesn't know when to copy back to host
            // to save changes to file
            // write_ptr = mmap(0, write_length, PROT_WRITE, MAP_PRIVATE, fd, read_length);
            write_ptr = mmap(0, write_length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        }
        else
        {
            read_ptr = fsrf->fsrf_malloc(read_length, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
            uint32_t length = read(fd, read_ptr, read_length);
            if (length != read_length)
            {
                std::cerr << "Problem reading\n";
                exit(1);
            }
            write_ptr = fsrf->fsrf_malloc(write_length, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
        }

        assert(read_ptr != MAP_FAILED);
        assert(write_ptr != MAP_FAILED);

        vert_ptr = (uint64_t)read_ptr;
        edge_ptr = vert_ptr + num_verts * 16;
        input_ptr = edge_ptr + num_edges * 8;
        output_ptr = (uint64_t)write_ptr;
    }

    virtual void start_fpga()
    {
        fsrf->cntrlreg_write(0x20, num_verts);
        fsrf->cntrlreg_write(0x30, num_edges);
        fsrf->cntrlreg_write(0x40, vert_ptr);
        fsrf->cntrlreg_write(0x50, edge_ptr);
        fsrf->cntrlreg_write(0x60, input_ptr);
        fsrf->cntrlreg_write(0x70, output_ptr);

        fsrf->cntrlreg_write(0x00, 0x1);
    }

    virtual void wait_for_fpga()
    {
        bool done = false;
        while (!done)
        {
            done = fsrf->cntrlreg_read(0x0) & 0x2;
        }
        fsrf->cntrlreg_write(0x00, 0x10);
    }

    virtual void copy_back_output()
    {
        uint64_t num_credits = 1;

        while (num_credits)
        {
            std::cout << "waiting\n";
            uint64_t fault = fsrf->read_tlb_fault();
            num_credits = fault >> 57;
        }

        // bring output data back to host
        uint32_t *output = (uint32_t *)write_ptr;
        uint32_t output_sum = 0;

        if (mode == FSRF::MODE::MMAP)
        {
            frsf.sync_device_to_host(write_ptr, write_length);
        }

        for (uint32_t i = 0; i < write_length / sizeof(uint32_t); i += 0x1000 / sizeof(uint32_t))
        {
            output_sum += output[i];
        }
        if (verbose)
            std::cout << "out sum: " << output_sum << "\n";
    }
};
