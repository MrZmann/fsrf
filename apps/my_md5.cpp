#include <errno.h>
#include <iostream>
#include "fsrf.h"

using namespace std::chrono;

int main(int argc, char *argv[])
{
    FSRF fsrf{0};
    void* buf = mmap(NULL, 0x2000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for(int i = 0; i < (1 << 12) / 64; i++){
        ((uint64_t*) buf)[i] = 0;
    }
    std::cout << "address of buf: " << (uint64_t*) buf << "\n";
    if(buf == MAP_FAILED)
        printf("Oh dear, something went wrong with mmap: \n\t%s\n", strerror(errno));

    fsrf.cntrlreg_write(0x10, (uint64_t) buf);  // src_addr
    fsrf.cntrlreg_write(0x18, 8);               // rd_credits
    fsrf.cntrlreg_write(0x20,2 * (1 << 12) / 64);  // num 64 byte words

    uint64_t val = 0;
    uint64_t count = 0;
    while(val != 2* (1 << 12) / 64){
        val = fsrf.cntrlreg_read(0x28);
        //if(count % 0x100000 == 0)
        if(val != 0)
            std:: cout << "Val: " << val << "\n";
        count++;
    }

    val = fsrf.cntrlreg_read(0x0);
    std::cout << "ab: " << val << "\n";
    val = fsrf.cntrlreg_read(0x8);
    std::cout << "cd: " << val << "\n";
	// end runs
	// important -> polls on completion register
	// util.finish_runs(aos, end, 0x28, true, configs[0].num_words);
	// important -> answer lives in cntrlreg_read 0x0, 0x8


	return 0;
}
