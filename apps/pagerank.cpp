#include "arg_parse.h"
#include "fsrf.h"

struct config {
	uint64_t num_verts;
	uint64_t num_edges;
	uint64_t vert_ptr;
	uint64_t edge_ptr;
	uint64_t input_ptr;
	uint64_t output_ptr;
	void *read_ptr;
	void *write_ptr;
};

int main(int argc, char *argv[]) {
    ArgParse argsparse(argc, argv);
    uint64_t app = argsparse.getAppId();
    FSRF::MODE mode = argsparse.getMode();
    bool debug = argsparse.getVerbose();

    FSRF fsrf{app, mode, debug};
	config configs[4];
	
	configs[0].num_verts = 1000448;
	configs[0].num_edges = 3105792;
	uint64_t read_length, write_length;
	read_length = configs[0].num_verts*(16+8) + configs[0].num_edges*8;
	write_length = configs[0].num_verts*8;
	
    int fd = open("/home/centos/fsrf/inputs/webbase-1M/webbase-1M.bin", O_RDWR);
    assert(fd != -1);

    configs[app].read_ptr = mmap(0, read_length, PROT_READ, MAP_SHARED, fd, 0);
    // limitation - transparent version doesn't know when to copy back to host
    // to save changes to file
    configs[app].write_ptr = mmap(0, write_length, PROT_WRITE, MAP_PRIVATE, fd, read_length);

    assert(configs[app].read_ptr != MAP_FAILED);
    assert(configs[app].write_ptr != MAP_FAILED);

    configs[app].vert_ptr = (uint64_t)configs[app].read_ptr;
    configs[app].edge_ptr = configs[app].vert_ptr + configs[0].num_verts*16;
    configs[app].input_ptr = configs[app].edge_ptr + configs[0].num_edges*8;
    configs[app].output_ptr = (uint64_t)configs[app].write_ptr;

	// start runs
    fsrf.cntrlreg_write(0x20, configs[app].num_verts);     
    fsrf.cntrlreg_write(0x30, configs[app].num_edges);
    fsrf.cntrlreg_write(0x40, configs[app].vert_ptr);
    fsrf.cntrlreg_write(0x50, configs[app].edge_ptr);
    fsrf.cntrlreg_write(0x60, configs[app].input_ptr);
    fsrf.cntrlreg_write(0x70, configs[app].output_ptr);


    fsrf.cntrlreg_write(0x00, 0x1);
    bool done = false;
    while (!done)
    {
        done = fsrf.cntrlreg_read(0x0) & 0x2;
    }

    // end run
	fsrf.cntrlreg_write(0x00, 0x10);
	
	return 0;
}

