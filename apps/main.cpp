#include <iostream>
#include <stdio.h>
#include "Bench.h"

int main(int argc, char *argv[])
{

    ArgParse argsparse(argc, argv);
    Bench *bench = new Aes(argsparse);
    bench->setup();
    bench->start_fpga();
    bench->wait_for_fpga();
    bench->copy_back_output();
}
