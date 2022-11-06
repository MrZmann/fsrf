#include "arg_parse.h"
#include "fsrf.h"

class Bench
{
    FSRF *fsrf = nullptr;
    FSRF::MODE mode;
    int verbose;
    uint64_t app_id;

    Bench(Argparse &argparse) : mode(FSRF::MODE::NONE), verbose(false), app_id(~0L)
    {
        mode = argparse.getMode();
        verbose = argparse.getVerbose();
        app_id = argparse.getAppId();
        fsrf = new FSRF(app_id, mode, verbose);
    }

    ~Bench()
    {
        delete fsrf;
    }

    virtual void setup() = 0;
    virtual void start_fpga() = 0;
    virtual void wait_for_fpga() = 0;
    virtual void copy_back_output() = 0;
};