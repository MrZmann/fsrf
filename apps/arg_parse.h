#pragma once
#include <getopt.h>
#include "fsrf.h"

class ArgParse
{
    FSRF::MODE mode;
    int verbose;
    int app_id;

public:
    ArgParse(int argc, char **argv) : mode(FSRF::MODE::NONE), verbose(false), app_id(-1)
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

    int getAppId()
    {
        return app_id;
    }

private:
    void read_args(int argc, char **argv)
    {
        int opt;
        while ((opt = getopt(argc, argv, ":ma:v")) != -1)
        {
            switch (opt)
            {
            case 'v':
                verbose = 1;
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
            case 'a':
                app_id = atoi(optarg);
                break;
            default:
                printf("unknown option: %c\n", optopt);
                break;
            }
        }
        if (app_id < 0 || app_id > 3)
        {
            std::cerr << "App id (-a) must be in range [0,3]\n";
            exit(1);
        }
        if (mode == FSRF::MODE::NONE)
        {
            std::cerr << "Mode (-m) must be one of [inv_read, inv_write, mmap]\n";
            exit(1);
        }
    }
};
