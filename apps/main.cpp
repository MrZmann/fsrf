#include "Aes.h"
#include "Bench.h"
#include "Md5.h"
#include "Nw.h"
#include "Pagerank.h"

int main(int argc, char *argv[])
{

    ArgParse argsparse(argc, argv);
    Bench *bench;

    const char *benchmarkNames[] = {"aes", "md5", "nw", "pagerank"};

    if (strcmp(argsparse.getBenchmarkName(), benchmarkNames[0]) == 0)
    {
        bench = new Aes(argsparse);
    }
    else if (strcmp(argsparse.getBenchmarkName(), benchmarkNames[1]) == 0)
    {
        bench = new Md5(argsparse);
    }
    else if (strcmp(argsparse.getBenchmarkName(), benchmarkNames[2]) == 0)
    {
        bench = new Nw(argsparse);
    }
    else if (strcmp(argsparse.getBenchmarkName(), benchmarkNames[3]) == 0)
    {
        bench = new Pagerank(argsparse);
    }
    else
    {
        std::cerr << "unsupported benchmark name\n";
        exit(1);
    }

    bench->setup();
    bench->start_fpga();
    bench->wait_for_fpga();
    bench->copy_back_output();
    delete bench;
}
