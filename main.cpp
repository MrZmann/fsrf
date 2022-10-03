#include <iostream>
#include <memory>
#include <thread>

#include "fault_handler.h"

int main(int argc, char **argv)
{
    int app_id = 0;

    // flock for interprocess locking

    FaultHandler faultHandler(app_id);
    std::thread fsrf_fault_handler(&FaultHandler::listen, faultHandler);

    fsrf_fault_handler.join();
    std::cerr << "Fault handler failed.";
    return -1;
}