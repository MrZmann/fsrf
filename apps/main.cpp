#include <chrono>

#include "Aes.h"
#include "Bench.h"
#include "Md5.h"
#include "Nw.h"
#include "Pagerank.h"

using namespace std::chrono;

int main(int argc, char *argv[])
{

    high_resolution_clock::time_point very_beginning, very_end;
    very_beginning = high_resolution_clock::now();

    high_resolution_clock::time_point start, end;

    start = high_resolution_clock::now();
    ArgParse argsparse(argc, argv);
    Bench *bench;
    end = high_resolution_clock::now();

    start = high_resolution_clock::now();
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

    end = high_resolution_clock::now();
    // auto construct_fsrf_time = end - start;

    start = high_resolution_clock::now();
    bench->setup();
    end = high_resolution_clock::now();
    // auto setup_time = end - start;

    bench->start_fpga();

    start = high_resolution_clock::now();
    bench->wait_for_fpga();
    end = high_resolution_clock::now();
    auto fpga_execution_time = end - start;

    start = high_resolution_clock::now();
    bench->copy_back_output();
    end = high_resolution_clock::now();
    // auto copy_back_time = end - start;

    very_end = high_resolution_clock::now();
    auto end_to_end_time = very_end - very_beginning;

    std::cout << "FPGA_EXECUTION, " << fpga_execution_time.count() * microseconds::period::num / microseconds::period::den << "\n";
    std::cout << "E2E, " << end_to_end_time.count() * microseconds::period::num / microseconds::period::den << "\n";

/*
    std::cout << "FSRF_CONSTRUCTOR," << construct_fsrf_time.count() * microseconds::period::num / microseconds::period::den << "\n";
    std::cout << "SETUP, " << setup_time.count() * microseconds::period::num / microseconds::period::den << "\n";
    std::cout << "COPY_BACK, " << copy_back_time.count() * microseconds::period::num / microseconds::period::den << "\n";
*/

    delete bench;
}
