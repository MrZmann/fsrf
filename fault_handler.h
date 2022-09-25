#pragma once

#include "request.h"
#include "queue.h"

class FaultHandler {
private:
    int app_id;
public:
    FaultHandler(int app_id) : app_id(app_id) { }

    void listen(){
        while(true){
            /*
            read tlb
            handle fault
            send response
            */
        }
    } 

};