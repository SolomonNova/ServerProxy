/*
    File name: http.h
    Creation date: 10-12-25
    Author: Solomon
*/

#ifndef HTTP_PARSER_H 
#define HTTP_PARSER_H 

#include <stddef.h> // provides size_t

typedef struct HEADER_KEY_VALUE {
    char* szKey;
    char* szValue;
} HEADER_KEY_VALUE;

typedef struct HEADERS   
{
    HEADER_KEY_VALUE* hkv_arrEntries;  // array of key-value pairs
    size_t            iCount;
    size_t            iCapacity;
} HEADERS;

typedef struct REQUEST_INFO
{
    // original request info
    char*    m_pOriginalRequest;        // pointer to the orignal request string
    size_t         m_iTotalRawBytes;    // tells how many raw bytes were received in the original http request

    // request-line info
    char*          m_szMethod;          // pointer to szRequestBuffer (do not free)
    char*          m_szPath;            // pointer to szRequestBuffer (do not free)
    char*          m_szVersion;         // pointer to szRequestBuffer (do not free)

    // headers info
    HEADERS*       m_h_headers;

    // body info
    char*          m_szBody;            // string containing the bytes of the body
    size_t         m_iBodyLength;
    HEADERS*       m_h_trailerHeaders;
    
    // general info about the request
    char*          m_pRequestStart;
    char*          m_pHeadersStart;
    char*          m_pBodyStart;
    char*          m_pRequestEnd;
} REQUEST_INFO;

typedef enum
{
    PARSE_SUCCESS = 0,
    ERR_NULL_CHECK_FAILED,
    ERR_EMPTY_REQUEST,
    ERR_INVALID_METHOD,
    ERR_INVALID_PATH,
    ERR_INVALID_PROTOCOL,
    ERR_CALLOC_FAILED,
    ERR_INVALID_FORMAT,
    ERR_OUT_OF_BOUNDS,
    ERR_REQUEST_LINE_PARSE_FAILED,
    ERR_HEADERS_PARSE_FAILED,
    ERR_BODY_PARSE_FAILED,
} PARSE_RESULT;

/*===================================== MAIN FUNCTIONS =====================================*/

PARSE_RESULT launch_parser      (REQUEST_INFO* ri_requestInfo, char* pBytestream);
PARSE_RESULT parse_request_line (REQUEST_INFO* ri_requestInfo, char* szRequestBuffer);
PARSE_RESULT parse_headers      (REQUEST_INFO* ri_requestInfo, char* szRequestBuffer);
PARSE_RESULT parse_body         (REQUEST_INFO* ri_requestInfo, char* szRequestBuffer);
PARSE_RESULT decode_chunked_body(REQUEST_INFO* ri_requestInfo, char* pBodyStart); // fills the REQUES_INFO struct if body is chunked (Transfer-Encoding: Chunked )

/*==================================== Helper Functions =====================================*/

void print_request_info(REQUEST_INFO* ri_requestInfo);


#endif
