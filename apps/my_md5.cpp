#include <errno.h>
#include <iostream>
#include "fsrf.h"

using namespace std::chrono;

int main(int argc, char *argv[])
{
    FSRF fsrf{0};
    void* buf = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(buf == MAP_FAILED)
        printf("Oh dear, something went wrong with mmap: \n\t%s\n", strerror(errno));

    std::cout << "\nAPP: giving src_addr == " << (uint64_t*) buf << "\n\n";

    fsrf.cntrlreg_write(0x10, (uint64_t) buf);  // src_addr
    fsrf.cntrlreg_write(0x18, 8);               // rd_credits
    fsrf.cntrlreg_write(0x20, (1 << 12) / 64);  // num 64 byte words

    uint64_t val = 0;
    uint64_t count = 0;
    while(val != (1 << 12) / 64){
        val = fsrf.cntrlreg_read(0x28);
        //if(count % 0x100000 == 0)
        if(val != 0)
            std:: cout << "Val: " << val << "\n";
        count++;
    }

	// end runs
	// important -> polls on completion register
	// util.finish_runs(aos, end, 0x28, true, configs[0].num_words);
	// important -> answer lives in cntrlreg_read 0x0, 0x8


	return 0;
}
