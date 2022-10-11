#include "fsrf.h"

using namespace std::chrono;

int main(int argc, char *argv[])
{
    FSRF fsrf{0};
    void* buf = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_ANON, -1, 0);

    /*
    // important, src_addr -> 4k aligned
    aos[app]->aos_cntrlreg_write(0x10, configs[app].src_addr);
    aos[app]->aos_cntrlreg_write(0x18, configs[0].rd_credits);

    // important -> number of 64 byte words to access
    aos[app]->aos_cntrlreg_write(0x20, configs[0].num_words);
    */

	// end runs
	// important -> polls on completion register
	// util.finish_runs(aos, end, 0x28, true, configs[0].num_words);
	// important -> answer lives in cntrlreg_read 0x0, 0x8


	return 0;
}
