#include "arg_parse.h"
#include "Bench.h"

class Pagerank : public Bench
{
    // uint64_t num_verts = 1000448;
    // uint64_t num_edges = 3105792;
    uint64_t num_verts = 68863488;
    uint64_t num_edges = 143415296;
    uint64_t vert_ptr;
    uint64_t edge_ptr;
    uint64_t input_ptr;
    uint64_t output_ptr;
    void *read_ptr;
    void *write_ptr;

    uint64_t read_length, write_length;

public:
    Pagerank(ArgParse argparse) : Bench(argparse) {}

    virtual void setup()
    {
        read_length = num_verts * (16 + 8) + num_edges * 8;
        write_length = num_verts * 8;

        // int fd = open("/home/centos/fsrf/inputs/webbase-1M/webbase-1M.bin", O_RDWR);
        int fd = open("/home/centos/fsrf/inputs/mawi_201512020030/mawi_201512020030.bin", O_RDWR | O_LARGEFILE);
        assert(fd != -1);

        if (mode == FSRF::MODE::INV_READ || mode == FSRF::MODE::INV_WRITE)
        {
            read_ptr = mmap(0, read_length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_NORESERVE, fd, 0);
            // limitation - transparent version doesn't know when to copy back to host
            // to save changes to file
            // write_ptr = mmap(0, write_length, PROT_WRITE, MAP_PRIVATE, fd, read_length);
            write_ptr = mmap(0, write_length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        }
        else
        {
            if(mode == FSRF::MODE::MMAP)
                read_ptr = fsrf->fsrf_malloc(read_length, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
            else    
                read_ptr = fsrf->fsrf_malloc_managed(read_length, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
            uint64_t length = 0;
            
            while (length != read_length) {
                length += read(fd, (void*) ((uint64_t)read_ptr + length), read_length - length);
                std::cout << "len: " << length << "\n";
            }
            if (length != read_length)
            {
                std::cerr << "Intended length: " << length << ", desired length " << read_length << "\n";
                std::cerr << "Problem reading\n";
                exit(1);
            }
            if(mode == FSRF::MODE::MMAP)
                write_ptr = fsrf->fsrf_malloc(write_length, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
            else
                write_ptr = fsrf->fsrf_malloc_managed(write_length, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
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
        uint64_t *output = (uint64_t *)write_ptr;
        uint64_t output_sum = 0;

        if (mode == FSRF::MODE::MMAP)
        {
            fsrf->sync_device_to_host(output, write_length);
        }
        
        else 
        {
            for (uint64_t i = 0; i < write_length / sizeof(uint64_t); i += 0x1000 / sizeof(uint64_t))
            {
                output_sum += output[i];
            }
            if (verbose)
                std::cout << "out sum: " << output_sum << "\n";
            assert(output_sum == 36028887531252117);
        }
    }
};
