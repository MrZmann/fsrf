#pragma once
#include <stdint.h>

enum class fsrf_command {
    CNTRLREG_READ_REQUEST,
    CNTRLREG_READ_RESPONSE,
    CNTRLREG_WRITE_REQUEST,
    CNTRLREG_WRITE_RESPONSE,
    SET_MODE_REQUEST,
    SET_MODE_RESPONSE
};

enum class fsrf_errcode {
    SUCCESS = 0,
    RETRY, // for reads
    ALIGNMENT_FAILURE,
    PROTECTION_FAILURE,
    APP_DOES_NOT_EXIST,
    TIMEOUT,
    UNKNOWN_FAILURE
};

struct fsrf_cntrlreg_read_request_packet {
    uint64_t slot_id;
    uint64_t app_id;
    uint64_t addr64;
};

struct fsrf_cntrlreg_read_response_packet {
    uint64_t data64;
};

struct fsrf_cntrlreg_write_request_packet {
    uint64_t slot_id;
    uint64_t app_id;
    uint64_t addr64;
    uint64_t data64; 
};

struct fsrf_set_mode_request_packet {
    uint64_t app_id;
    uint64_t mode;
    uint64_t data;
};

struct fsrf_request {
    fsrf_command command;
    union packet {
        fsrf_cntrlreg_read_request_packet read_request;
        fsrf_cntrlreg_read_response_packet read_response;
        fsrf_cntrlreg_write_request_packet write_response;
        fsrf_set_mode_request_packet set_mode_request;
    };

};