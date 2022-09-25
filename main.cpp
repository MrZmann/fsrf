#include <iostream>
#include <memory>
#include <thread>

#include "fault_handler.h"
#include "queue.h"
#include "request.h"
#include "request_handler.h"

int main(int argc, char** argv){
    int app_id = 0;

    auto request_queue = std::make_shared<Queue<fsrf_request>>();
    RequestHandler requestHandler(app_id, request_queue);
    std::thread fsrf_request_handler(&RequestHandler::listen, requestHandler);

    FaultHandler faultHandler(app_id);
    std::thread fsrf_fault_handler(&FaultHandler::listen, faultHandler);

    fsrf_request_handler.join();
    std::cerr << "Request handler failed.";
    return -1;
}