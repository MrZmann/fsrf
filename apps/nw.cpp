#include <iostream>

#include "arg_parse.h"
#include "fsrf.h"

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
    ArgParse argsparse(argc, argv);
    uint64_t app = argsparse.getAppId();
    FSRF::MODE mode = argsparse.getMode();
    bool debug = argsparse.getVerbose();

    FSRF fsrf {app, mode, debug};

	const uint64_t s_ratio = 512/128;
	const uint64_t sc_ratio = 512/8;
	
	uint64_t length0 = 10;
	uint64_t length1 = 11;
	
    config configs[4];
	configs[0].s0_words = 1 << length0;
	configs[0].s1_words = 1 << length1;
	configs[0].s1_credit = 256;
	configs[0].sc_count = (configs[0].s0_words*s_ratio) * (configs[0].s1_words*s_ratio);
	configs[0].sc_words = (configs[0].sc_count+sc_ratio-1) / sc_ratio;
	
	
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    uint64_t offset = 0;
    
    length0 = configs[0].s0_words * 64;

    void* addr;

    if(mode == FSRF::MODE::MMAP)
    {
        addr = fsrf.fsrf_malloc(length0, PROT_READ, flags);
    }
    else
    {
        addr = mmap(0, length0, PROT_READ, flags, -1, 0);
        if(addr == MAP_FAILED) { std::cerr << "Map failed\n"; exit(1); }
    }
    configs[app].s0_addr = (uint64_t)addr;
    offset += length0;

    length1 = configs[0].s1_words * 64;
    
    if(mode == FSRF::MODE::MMAP)
    {
        addr = fsrf.fsrf_malloc(length1, PROT_READ, flags);
    }
    else
    {
        addr = mmap(0, length1, PROT_READ, flags, -1, 0);
        if(addr == MAP_FAILED) { std::cerr << "Map failed\n"; exit(1); }
    }

    configs[app].s1_addr = (uint64_t)addr;
    offset += length1;

    length1 = configs[0].sc_words * 64;

    if(mode == FSRF::MODE::MMAP)
    {
        addr = fsrf.fsrf_malloc(length1, PROT_WRITE, flags);
    }
    else
    {
        addr = mmap(0, length1, PROT_WRITE, flags, -1, 0);
        if(addr == MAP_FAILED) { std::cerr << "Map failed\n"; exit(1); }
    }

    configs[app].sc_addr = (uint64_t)addr;
	
    fsrf.cntrlreg_write(0x00, configs[app].s0_addr);
    fsrf.cntrlreg_write(0x08, configs[0].s0_words);
    fsrf.cntrlreg_write(0x10, configs[app].s1_addr);
    fsrf.cntrlreg_write(0x18, configs[0].s1_words);
    fsrf.cntrlreg_write(0x20, configs[0].s1_credit);
    fsrf.cntrlreg_write(0x28, configs[0].sc_count);
    fsrf.cntrlreg_write(0x30, configs[app].sc_addr);
    fsrf.cntrlreg_write(0x38, configs[0].sc_words);
	
    uint64_t val = 1;
    while (val != 0)
    {
        val = fsrf.cntrlreg_read(0x48);
    }

    return 0;
}	
