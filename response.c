/*
    File name: response.c
    Created at: 15-01-25
    Author: Solomon
*/

#include <stddef.h>    // provides size_t
#include <string.h>     
#include "response.h"  // provides REQUEST_INFO
#include "http.h"      // provides REQUEST_INFO


#define MAX_RESPONSE_HEADER_SIZE 4096

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
int initialize_response_header_buffer(REQUEST_INFO* ri_requestInfo)
{
    if (!ri_requestInfo) return -1;    

    char buffer[MAX_RESPONSE_HEADER_SIZE];
    int offset = 0;

    offset = write_status_line(ri_requestInfo, buffer, offset);
    if (offset > MAX_RESPONSE_HEADER_SIZE) return -1;

    offset = write_headers(ri_requestInfo, buffer, offset);
    if (offset > MAX_RESPONSE_HEADER_SIZE) return -1;

    offset = write_final_crlf(ri_requestInfo, buffer, offset);
    if (offset > MAX_RESPONSE_HEADER_SIZE) return -1;

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
int write_status_line(REQUEST_INFO* ri_requestInfo, char* buffer, size_t iOffset)
{
    if (!ri_requestInfo || !buffer) return -1;
    
    // 1) Reject the unsupported versions
    if (strcmp(ri_requestInfo->m_szVersion, "HTTP/1.0") || strcmp(ri_requestInfo->m_szVersion, "HTTP/1.1"))
        return 505;
    
    
    memcpy(buffer + iOffset, ri_requestInfo->m_szVersion, 8);



    return  0;
}


