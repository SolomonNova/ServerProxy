/*
    File name: response.h
    Created at: 15-01-25
    Author: Solomon
*/

/*
    Each worker will have its own buffer
    That buffer will hold the status line + headers + last empty line
    The buffer will be sent to the client
    The body will be handled later
    
*/

#ifndef RESPONSE_H
#define RESPONSE_H

#include "http.h"
#include <stddef.h> // provides size_t
                    
typedef struct REQUEST_INFO REQUEST_INFO;

typedef enum
{
    RESPONSE_SUCCESS = 0,
    RESPONSE_FAIL_CLIENT_ERROR,
    RESPONSE_FAIL_SERVER_ERROR,
    RESPONSE_FAIL_NO_CONTENT,
} RESPONSE_RESULT;

int initialize_response_header_buffer(REQUEST_INFO* ri_requestInfo);
int write_status_line                (REQUEST_INFO* ri_requestInfo, char* buffer, size_t iOffset);
int write_headers                    (REQUEST_INFO* ri_requestInfo, char* buffer, size_t iOffset);
int write_final_crlf                 (REQUEST_INFO* ri_requestInfo, char* buffer, size_t iOffset);


#endif
