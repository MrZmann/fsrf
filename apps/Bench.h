#pragma once 
#include "arg_parse.h"
#include "fsrf.h"

class Bench
{
protected:
    FSRF *fsrf = nullptr;
    FSRF::MODE mode;
    int verbose;
    uint64_t app_id;
    int batch_size;

public:
    Bench(ArgParse argparse) : mode(FSRF::MODE::NONE), verbose(false), app_id(~0L)
    {
        mode = argparse.getMode();
        verbose = argparse.getVerbose();
        app_id = argparse.getAppId();
        batch_size = argparse.getBatchSize();
        fsrf = new FSRF(app_id, mode, verbose, batch_size);
    }

    virtual ~Bench()
    {
        delete fsrf;
    }

    virtual void setup() = 0;
    virtual void start_fpga() = 0;
    virtual void wait_for_fpga() = 0;
    virtual void copy_back_output() = 0;
};
