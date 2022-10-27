#include <chrono>
#include "aos.hpp"
#include "utils.hpp"

using namespace std::chrono;

struct config {
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
};

int main(int argc, char *argv[]) {
	const uint64_t s_ratio = 512/128;
	const uint64_t sc_ratio = 512/8;
	
	uint64_t length0 = 10;
	uint64_t length1 = 11;
	
	configs[0].s0_words = 1 << length0;
	configs[0].s1_words = 1 << length1;
	configs[0].s1_credit = 256;
	configs[0].sc_count = (configs[0].s0_words*s_ratio) * (configs[0].s1_words*s_ratio);
	configs[0].sc_words = (configs[0].sc_count+sc_ratio-1) / sc_ratio;
	
	
    {
        uint64_t app = 0;
		int flags = populate ? MAP_POPULATE : 0;
		uint64_t offset = 0;
		
		length0 = configs[0].s0_words * 64;
		void* addr = mmap(0, length0, PROT_READ, flags, fd[app], offset);
		configs[app].s0_addr = (uint64_t)addr;
		offset += length0;
		length1 = configs[0].s1_words * 64;
		aos[app]->aos_mmap(addr, length1, PROT_READ, flags, fd[app], offset);
		configs[app].s1_addr = (uint64_t)addr;
		offset += length1;
		length1 = configs[0].sc_words * 64;
		aos[app]->aos_mmap(addr, length1, PROT_WRITE, flags, fd[app], offset);
		configs[app].sc_addr = (uint64_t)addr;
	}
	
	// start runs
	start = high_resolution_clock::now();
	for (uint64_t app = 0; app < num_apps; ++app) {
		aos[app]->aos_cntrlreg_write(0x00, configs[app].s0_addr);
		aos[app]->aos_cntrlreg_write(0x08, configs[0].s0_words);
		aos[app]->aos_cntrlreg_write(0x10, configs[app].s1_addr);
		aos[app]->aos_cntrlreg_write(0x18, configs[0].s1_words);
		aos[app]->aos_cntrlreg_write(0x20, configs[0].s1_credit);
		aos[app]->aos_cntrlreg_write(0x28, configs[0].sc_count);
		aos[app]->aos_cntrlreg_write(0x30, configs[app].sc_addr);
		aos[app]->aos_cntrlreg_write(0x38, configs[0].sc_words);
	}
	
	// end runs
	util.finish_runs(aos, end, 0x48, true, 0);
}	
