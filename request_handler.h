#pragma once

#include <memory>

#include "request.h"
#include "queue.h"

class RequestHandler {
private:
    int app_id;
    std::shared_ptr<Queue<fsrf_request>> request_queue;
public:
    RequestHandler(int app_id, std::shared_ptr<Queue<fsrf_request>> request_queue) :
        app_id(app_id), request_queue(request_queue) { }

    void listen(){
        while(true){
            if(request_queue->isEmpty()){
                continue;
            }

            fsrf_request req = request_queue->poll();

            // TODO: execute request
        }
    } 

};