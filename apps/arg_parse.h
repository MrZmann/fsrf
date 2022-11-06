#pragma once
#include <assert.h>
#include <getopt.h>
#include <iostream>
#include "fsrf.h"

class ArgParse
{
    FSRF::MODE mode;
    int verbose;
    bool need_app_id;
    uint64_t app_id;
    char *benchmark_name;

public:
    ArgParse(int argc, char **argv, bool need_app_id = true) : mode(FSRF::MODE::NONE), verbose(false), need_app_id(need_app_id), app_id(~0L), benchmark_name(nullptr)
    {
        read_args(argc, argv);
    }

    FSRF::MODE getMode()
    {
        return mode;
    }

    int getVerbose()
    {
        return verbose;
    }

    uint64_t getAppId()
    {
        assert(need_app_id);
        return app_id;
    }

    char *getBenchmarkName()
    {
        return benchmark_name;
    }

private:
    void read_args(int argc, char **argv)
    {
        int opt;
        while ((opt = getopt(argc, argv, "a:b:m:v")) != -1)
        {
            switch (opt)
            {
            case 'v':
                verbose = 1;
                break;
            case 'a':
                app_id = atoi(optarg);
                break;
            case 'b':
                benchmark_name = optarg;
                break;
            case 'm':
                if (strcmp("inv_read", optarg) == 0)
                {
                    mode = FSRF::MODE::INV_READ;
                }
                else if (strcmp("inv_write", optarg) == 0)
                {
                    mode = FSRF::MODE::INV_WRITE;
                }
                else if (strcmp("mmap", optarg) == 0)
                {
                    mode = FSRF::MODE::MMAP;
                }
                else
                {
                    printf("Unexpected mode!\n");
                    exit(1);
                }
                break;
            default:
                printf("unknown option: %c\n", optopt);
                break;
            }
        }
        if (app_id > 3 && need_app_id)
        {
            std::cerr << "App id (-a) must be in range [0,3]\n";
            exit(1);
        }
        if (mode == FSRF::MODE::NONE)
        {
            std::cerr << "Mode (-m) must be one of [inv_read, inv_write, mmap]\n";
            exit(1);
        }
        if (benchmark_name == nullptr)
        {
            std::cerr << "Benchmark name (-b) not specified.\n";
            exit(1);
        }
    }
};
