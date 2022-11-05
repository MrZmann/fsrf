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

	configs[app].num_verts = 1000448;
	configs[app].num_edges = 3105792;
	uint64_t read_length, write_length;
	read_length = configs[app].num_verts*(16+8) + configs[app].num_edges*8;
	write_length = configs[app].num_verts*8;
	
    int fd = open("/home/centos/fsrf/inputs/webbase-1M/webbase-1M.bin", O_RDWR);
    assert(fd != -1);

    if (mode == FSRF::MODE::INV_READ || mode == FSRF::MODE::INV_WRITE){
        configs[app].read_ptr = mmap(0, read_length, PROT_READ, MAP_SHARED, fd, 0);
        // limitation - transparent version doesn't know when to copy back to host
        // to save changes to file
        // configs[app].write_ptr = mmap(0, write_length, PROT_WRITE, MAP_PRIVATE, fd, read_length);
        configs[app].write_ptr = mmap(0, write_length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    } else {
        configs[app].read_ptr = fsrf.fsrf_malloc(read_length, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
        uint32_t length = read(fd, configs[app].read_ptr, read_length);
        if (length != read_length) {
            std::cerr << "Problem reading\n";
            exit(1);
        }
        configs[app].write_ptr = fsrf.fsrf_malloc(write_length, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE);
    }

    assert(configs[app].read_ptr != MAP_FAILED);
    assert(configs[app].write_ptr != MAP_FAILED);

    configs[app].vert_ptr = (uint64_t)configs[app].read_ptr;
    configs[app].edge_ptr = configs[app].vert_ptr + configs[app].num_verts*16;
    configs[app].input_ptr = configs[app].edge_ptr + configs[app].num_edges*8;
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

    uint64_t num_credits = 1;

    while(num_credits) {
        std::cout << "waiting\n";
        uint64_t fault = fsrf.read_tlb_fault();
        num_credits = fault >> 57;
    }

    // bring output data back to host
    uint32_t* input = (uint32_t*) configs[app].read_ptr;
    uint32_t* output = (uint32_t*) configs[app].write_ptr;
    uint32_t input_sum = 0;
    uint32_t output_sum = 0;

    for(uint32_t i = 0; i < read_length / sizeof(uint32_t); i += 0x1000/sizeof(uint32_t)){
        input_sum += input[i];
    }

    for(uint32_t i = 0; i < write_length / sizeof(uint32_t); i += 0x1000/sizeof(uint32_t)){
        output_sum += output[i];
    }
    if(debug) std::cout << "in sum: " << input_sum << "\n";
    if(debug) std::cout << "out sum: " << output_sum << "\n";
	return 0;
}

