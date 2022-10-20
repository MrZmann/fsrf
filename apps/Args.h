#include <getopt.h>
#include "fsrf.h"

class Args
{
    FSRF::MODE mode;
    int verbose;

public:
    Args(int argc argc, char **argv)
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

        if (mapType == 0)
        {
            printf("Must specify -m for mmap type at least once\n");
            exit(1);
        }

        if (verbose)
        {
            printf("Size of physical memory: %lu\n", get_mem_size());
            printf("clearCache: %d\n", clearCache);
            printf("competeForMemory: %d\n", competeForMemory);
            printf("doMemset: %d\n", doMemset);
            printf("opt_random_access: %d\n", opt_random_access);
            printf("mmap:");
            int useOr = 0;
            if (mapType & MAP_ANONYMOUS)
            {
                printf(" MAP_ANONYMOUS");
                useOr = 1;
            }
            if (mapType & MAP_SHARED)
            {
                if (useOr)
                    printf(" |");
                printf(" MAP_SHARED");
                useOr = 1;
            }
            if (mapType & MAP_PRIVATE)
            {
                if (useOr)
                    printf(" |");
                printf(" MAP_PRIVATE");
                useOr = 1;
            }
            if (mapType & MAP_POPULATE)
            {
                if (useOr)
                    printf(" |");
                printf(" MAP_POPULATE");
                useOr = 1;
            }
            printf("\n");
            printf("readTLB: %d\n", readTLB);
            printf("useRealFile: %d\n", useRealFile);
        }
    }
}