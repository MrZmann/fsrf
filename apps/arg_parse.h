#pragma once 
#include <getopt.h>
#include "fsrf.h"

class ArgParse 
{
    FSRF::MODE mode;
    int verbose;

public:
    ArgParse(int argc, char **argv) : mode(FSRF::MODE::NONE), verbose(false)
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

private:
    void read_args(int argc, char **argv)
    {
        int opt;
        while ((opt = getopt(argc, argv, ":m:v")) != -1)
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
            default:
                printf("unknown option: %c\n", optopt);
                break;
            }
        }
        if(mode == FSRF::MODE::NONE){
            std::cerr << "Mode (-m) must be one of [inv_read, inv_write, mmap]\n";
            exit(1);
        }
    }
};
