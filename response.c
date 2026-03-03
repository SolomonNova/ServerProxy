/*
    File name: response.c
    Created at: 15-01-25
    Author: Solomon
*/

#include <cstddef>
#include <iterator>
#include <stddef.h>     // provides size_t
#include <stdio.h>      // provides snprintf()
#include <string.h>     // provides strcmp(), strncasecmp()
#include <strings.h>
#include <sys/socket.h> // provides send()
#include "response.h"   // provides REQUEST_INFO
#include <time.h>       // provides type time_t, struct tm, gmtime_r(), strftime()
#include "http.h"       // provides REQUEST_INFO

#define MAX_RESPONSE_HEADER_SIZE 4096

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/* --------------------------- Helper Functions --------------------------- */
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
static int parse_result_to_http_status(PARSE_RESULT rc)
{
    switch (rc)
    {
        case ERR_INVALID_METHOD:               return 405;
        case ERR_INVALID_PROTOCOL:             return 505;
        case ERR_UNSUPPORTED_TRANSFER_ENCODING:return 501;
        case ERR_CALLOC_FAILED:                return 500;

        case ERR_EMPTY_REQUEST:
        case ERR_REQUEST_LINE_PARSE_FAILED:
        case ERR_HEADERS_PARSE_FAILED:
        case ERR_BODY_PARSE_FAILED:
        case ERR_INVALID_FORMAT:
        case ERR_OUT_OF_BOUNDS:
        default:                               return 400;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
static const char* http_reason_phrase(int status)
{
    switch (status) {
        case 400: return "Bad Request";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 505: return "HTTP Version Not Supported";
        default:  return "Error";
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
void send_parse_error_response(int iClientFd, const REQUEST_INFO* ri)
{
    if (!ri || iClientFd < 0) return;

    char buffer[512];
    int iStatus = parse_result_to_http_status(ri->m_parseResult);

    // if the version exists echo it or just use http 1.1
    const char* version = (ri->m_szVersion &&
                          (!strcmp(ri->m_szVersion, "HTTP/1.0") || 
                           !strcmp(ri->m_szVersion, "HTTP.1.1") ))
                          ? ri->m_szVersion : "HTTP/1.1";

    int n = snprintf(buffer, sizeof(buffer),
        "%s %d %s\r\n"
        "Connection: close\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        version,
        iStatus,
        http_reason_phrase(iStatus)
    );

    send(iClientFd,  buffer, n, 0);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
void send_simple_response
(
    int iClientFd,
    const REQUEST_INFO* ri,
    int iStatus,
    const char* reason,
    const char* body,
    size_t iBodyLen
)
{
    if (iClientFd < 0 || !reason) return;

    const char* szVersion = (ri &&
                            ri->m_szVersion &&
                            (!strcmp(ri->m_szVersion, "HTTP/1.0") || !strcmp(ri->m_szVersion, "HTTP/1.1"))) ? 
                            ri->m_szVersion : "HTTP/1.1";

    // send headers first, even if the body fails the headers must be sent 
    char buffer[512];
    int iHeaderSize = snprintf(buffer, sizeof(buffer),
                              "%s %d %s\r\n"
                              "Connection: close\r\n"
                              "Content-Length: %zu\r\n"
                              "\r\n",
        szVersion, iStatus, reason, iBodyLen
    );

    if (iHeaderSize < 0 || iHeaderSize >= (int)sizeof(buffer))
        return;

    // send the headers
    size_t iSent = 0;
    while (iSent < (size_t)iHeaderSize)
    {
        ssize_t n = send(iClientFd, buffer + iSent, iHeaderSize - iSent, 0);
        if (n <= 0) return;
        iSent += n; 
    }

    // send body now
    if (body && iBodyLen > 0)
    {
        iSent = 0;
        while (iSent < iBodyLen)
        {
            ssize_t n = send(iClientFd, body + iSent, iBodyLen - iSent, 0);
            if (n <= 0) return;
            iSent += n;
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
static int header_key_eq(const char *a, const char *b)
{
    /* Helper: case-insensitive header key compare (exact match) */
    if (!a || !b) return 0;
    size_t la = strlen(a);
    size_t lb = strlen(b);
    if (la != lb) return 0;
    return (strncasecmp(a, b, la) == 0);
}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
/* --------------------------- Main Functions --------------------------- */
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////


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
int write_status_line(REQUEST_INFO* ri, char* buffer, size_t iOffset)
{
    if (!ri || !buffer) return -1;


    /* Status line format -> "version status reason\r\n" */

    /* choose a sensible HTTP version: echo valid request version or fall back */
    const char* version = "HTTP/1.1";
    if (ri->m_szVersion &&
        (!strcmp(ri->m_szVersion, "HTTP/1.0") ||
         !strcmp(ri->m_szVersion, "HTTP/1.1")))
    {
        version = ri->m_szVersion;
    }

    /* decide status code:
       - if parser succeeded, assume 200 OK (application can override by not using this helper)
       - if parser failed, map parse result to an appropriate error status */
    int status = (ri->m_parseResult == PARSE_SUCCESS) ?
                 200 : parse_result_to_http_status(ri->m_parseResult);

    const char* reason = (status == 200) ? "OK" : http_reason_phrase(status);

    /* remaining space in the header buffer */
    size_t remaining = MAX_RESPONSE_HEADER_SIZE > iOffset ? MAX_RESPONSE_HEADER_SIZE - iOffset : 0;
    if (remaining == 0) return -1;

    /* snprintf returns the number of characters that would have been written (excluding '\0') */
    int wrote = snprintf(buffer + iOffset, remaining, "%s %d %s\r\n", version, status, reason);
    if (wrote < 0) return -1;

    /* if wrote >= remaining, the output was truncated -> error */
    if ((size_t)wrote >= remaining) return -1;

    /* return new offset (old + bytes written) */
    return (int)(iOffset + (size_t)wrote);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
int write_heades(REQUEST_INFO* ri, char* buffer, size_t iOffset)
{
    if (!ri || !buffer) return -1;
    
    size_t offset = iOffset;
    size_t iRemaning = (MAX_RESPONSE_HEADER_SIZE > iOffset) ? (MAX_RESPONSE_HEADER_SIZE - iOffset) : 0;
    if (iRemaning == 0) return -1;
    
    int iWrote;
 
    // 1) write the Date header
    // syntax for Date header is -> Date: <day-name>, <day> <month> <year> <hour>:<minute>:<second> GMT
    
    /*--------------------------------------- Date Header --------------------------------------*/
    {
        time_t now = time(NULL);
        struct tm gm; // gm means `GreenWich Mean` which is now called UTC time, tm means time components
        
        if (gmtime_r(&now, &gm) == NULL) return -1;

        char dateBuffer[64];
        // strftime() means string format time, fills dateBuffer with string values of time using gm
        if (strftime(dateBuffer, sizeof(dateBuffer), "%a, %d %b %Y %H:%M:%S GMT", &gm) == 0)
            return -1;

        iWrote = snprintf(buffer + offset, iRemaning, "Date: %s\r\n", dateBuffer);
        if (iWrote < 0 || (size_t)iWrote > iRemaning) return -1;
        offset += (size_t)iWrote;
        iRemaning += (size_t)iWrote;
    }
    /*------------------------------------------------------------------------------------------*/

    // 2) Write Server header 
    /*--------------------------------------- Server header --------------------------------------*/
    {
        const char* szServerHeader = "Server: Solomon/1.0\r\n";
        size_t iHeaderLen = strlen(szServerHeader);

        if (iHeaderLen >= iRemaning) return -1;
        memcpy(buffer + offset, szServerHeader, iHeaderLen);
        offset += iHeaderLen;
        iRemaning -= iHeaderLen;
    }
    /*------------------------------------------------------------------------------------------*/

    // 3) Connection header
    /*-------------------------------------- Connection header --------------------------------------*/
    int iFoundConnectionHeader = 0;
    int iRequestWantsClose     = 0;
    int iRequestWantsKeepAlive = 0;

    for (size_t i = 0; i < ri->m_headers.count; ++i)
    {
        const char* key   = ri->m_headers.entries[i].szKey;
        const char* value = ri->m_headers.entries[i].szValue;
        
        if (!key || !value) continue;

        if (strncasecmp(key, "Connection", 10) == 0)
        {
            iFoundConnectionHeader = 1;
            if (strcasestr(value, "close"))      iRequestWantsClose = 1;
            if (strcasestr(value, "keep-alive")) iRequestWantsKeepAlive = 1;
        }
    }

    int iSendClose = 0;
    if (iFoundConnectionHeader && iRequestWantsClose) iSendClose = 1;
    else if (ri->m_szVersion && strcmp(ri->m_szVersion, "HTTP/1.0") == 0) // HTTP/1.0 defaults to close unless clinet explicityly asks keep-alive
        { if (!iRequestWantsKeepAlive) iSendClose = 1; }

    else iSendClose = 0; // HTTP/1.1 and above defauls to keep-alive

    if (iSendClose) iWrote = snprintf(buffer + offset, iRemaning, "Connection: close\r\n");
    else iWrote = snprintf(buffer + offset, iRemaning, "Connection: keep-alive\r\n");

    if (iWrote < 0 || (size_t)iWrote >= iRemaning) return -1;
    offset += iWrote;
    iRemaning -= iWrote;     
    /*------------------------------------------------------------------------------------------*/

    /*4) Minimal Vary header if request advertised Accept-Encoding (helps caches)
     * Only add when we actually compress; we don't compress here, but adding
     * Vary unconditionally is not ideal. We'll add Vary only if client sent Accept-Encoding.
    */
    
    /*-------------------------------------- Vary header --------------------------------------*/
    for (size_t i = 0; i < ri->m_headers.count; ++i)
    {
        const char* key = ri->m_headers.entries[i].szKey;
        if (!key) continue;
        if (strncasecmp(key, "accept-encoding", 15) == 0)
        {
            iWrote = snprintf(buffer + offset, iRemaning, "Vary: Accept-Encoding\r\n");
            if (iWrote < 0 || (size_t)iWrote >= iRemaning) return -1;
            offset += (size_t)iWrote;
            iRemaning -= (size_t)iWrote;
            break;
        }
    }
    /*------------------------------------------------------------------------------------------*/

    return (size_t)offset;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
int write_final_crlf(REQUEST_INFO* ri, char* buffer, size_t iOffset)
{
    if (!ri || !buffer) return -1;
    const char* finalCRLF = "\r\n";
    
    int iRemaning = (MAX_RESPONSE_HEADER_SIZE > iOffset) ? MAX_RESPONSE_HEADER_SIZE - iOffset : 0;
    if (iRemaning == 0) return -1;

    int offset = iOffset;
    int iWrote;
    
    iWrote = snprintf(buffer + offset, iRemaning, "%s", finalCRLF);
    if (iWrote < 0 || (size_t)iWrote >= iRemaning) return -1;
    offset += (size_t)iWrote;

    return offset;
}
