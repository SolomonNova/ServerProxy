/*
    File name: http.c
    Creation date: 10-12-25
    Author: Solomon
*/

#include <string.h>  // provides memcmp(), strcmp()
#include <stdio.h>   // provides printf()
#include <stdbool.h> // provides bool type
#include <stdlib.h>  // all dynamic memory allocation
#include "http.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//======================================== MAIN PARSER API ========================================//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * NOTE about this implementation and ownership:
 * - Caller (worker) provides a read buffer (pBytestream) and its length.
 * - Parser stores a pointer to that buffer in ri->m_pRawRequest and treats most
 *   parsed strings as borrowed views into that buffer (zero-copy).
 * - Parser DOES mutate the worker buffer in-place (it inserts '\0' to terminate
 *   lines) — this is by design to keep parsing zero-copy and fast.
 * - Parser allocates header arrays (ri->m_headers.entries and
 *   ri->m_trailerHeaders.entries) as needed and will allocate/own a decoded
 *   body buffer only for chunked decoding. free_request_info() will free those.
 *
 * - The parser never frees the worker buffer.
 */

/* ---------- small helpers ---------- */

/* Find the first occurrence of `needle` inside `haystack` but limited to haystack_len.
 * Returns pointer to the match inside haystack, or NULL if not found.
 */
static char* find_substr_in_bounds
(
    const char* haystack,
    size_t      haystack_len,
    const char* needle
)
{
    if (!haystack || !needle) return NULL;

    size_t needle_len = strlen(needle);
    if (needle_len == 0 || haystack_len < needle_len) return NULL;

    const char* end_search = haystack + (haystack_len - needle_len) + 1;
    for (const char* p = haystack; p < end_search; ++p)
    {
        if (memcmp(p, needle, needle_len) == 0)
            return (char*)p;
    }
    return NULL;
}

/* Safe helper: returns remaining bytes from a pointer into the raw buffer.
   If ptr is outside range, returns 0. */
static size_t remaining_from_pointer
(
    const REQUEST_INFO* ri,
    const char*         ptr
)
{
    if (!ri || !ri->m_pRawRequest || !ptr) return 0;
    const char* base = ri->m_pRawRequest;
    if (ptr < base) return 0;
    size_t offset = (size_t)(ptr - base);
    if (offset > ri->m_iTotalRawBytes) return 0;
    return ri->m_iTotalRawBytes - offset;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
PARSE_RESULT launch_parser
(
    REQUEST_INFO* ri,
    const char*   pBytestream,
    size_t        bytestream_len
)
{
    if (!ri || !pBytestream) {
        if (ri) ri->m_parseResult = ERR_NULL_CHECK_FAILED;
        return ERR_NULL_CHECK_FAILED;
    }

    /* Initialize REQUEST_INFO fields conservatively so parser can own allocations */
    ri->m_pRawRequest            = pBytestream;
    ri->m_iTotalRawBytes         = bytestream_len;

    ri->m_szMethod               = NULL;
    ri->m_szPath                 = NULL;
    ri->m_szVersion              = NULL;

    ri->m_headers.entries        = NULL;
    ri->m_headers.count          = 0;
    ri->m_headers.capacity       = 0;

    ri->m_trailerHeaders.entries = NULL;
    ri->m_trailerHeaders.count   = 0;
    ri->m_trailerHeaders.capacity= 0;

    ri->m_szBody                 = NULL;
    ri->m_iBodyLength            = 0;

    ri->m_pRequestStart          = NULL;
    ri->m_pHeadersStart          = NULL;
    ri->m_pBodyStart             = NULL;
    ri->m_pRequestEnd            = NULL;

    ri->m_is_chunked             = false;
    ri->m_body_is_heap_allocated = false;

    /* initialize parse result to success; parser stages will overwrite on error */
    ri->m_parseResult = PARSE_SUCCESS;

    /* parse stages (each uses ri->m_pRawRequest directly) */
    PARSE_RESULT rc;

    rc = parse_request_line(ri);
    if (rc != PARSE_SUCCESS) {
        ri->m_parseResult = rc;
        return rc;
    }

    rc = parse_headers(ri);
    if (rc != PARSE_SUCCESS) {
        ri->m_parseResult = rc;
        return rc;
    }

    rc = parse_body(ri);
    if (rc != PARSE_SUCCESS) {
        ri->m_parseResult = rc;
        return rc;
    }

    ri->m_parseResult = PARSE_SUCCESS;

    print_request_info(ri);

    return PARSE_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
PARSE_RESULT parse_request_line
(
    REQUEST_INFO* ri
)
{
    if (!ri || !ri->m_pRawRequest) return ERR_EMPTY_REQUEST;

    /* we will mutate the buffer in-place; cast away const to match existing style */
    char* szRequestBuffer = (char*)ri->m_pRawRequest;
    size_t total_len = ri->m_iTotalRawBytes;

    ri->m_pRequestStart = ri->m_pRawRequest;

    /* find the first CRLF within bounds */
    char* pLineEnd = find_substr_in_bounds(szRequestBuffer, total_len, "\r\n");
    if (!pLineEnd) return ERR_NULL_CHECK_FAILED;

    size_t iOffset = (size_t)(pLineEnd - szRequestBuffer) + 2;
    if (iOffset > total_len) return ERR_INVALID_FORMAT;

    ri->m_pHeadersStart = (const char*)(pLineEnd + 2);

    /* Temporarily terminate the first line so strtok_r works */
    char saved_char = *pLineEnd;
    *pLineEnd = '\0';

    char* pSavePtr = NULL;

    ri->m_szMethod = strtok_r(szRequestBuffer, " ", &pSavePtr);
    ri->m_szPath   = strtok_r(NULL, " ", &pSavePtr);
    ri->m_szVersion= strtok_r(NULL, " ", &pSavePtr);

    if (!ri->m_szMethod) { *pLineEnd = saved_char; return ERR_INVALID_METHOD; }
    if (!ri->m_szPath)   { *pLineEnd = saved_char; return ERR_INVALID_PATH; }
    if (!ri->m_szVersion){ *pLineEnd = saved_char; return ERR_INVALID_PROTOCOL; }

    /* ensure no extra token after version */
    if (strtok_r(NULL, " ", &pSavePtr) != NULL)
    {
        *pLineEnd = saved_char;
        return ERR_INVALID_FORMAT;
    }

    /* restore byte we clobbered (should be '\r') */
    *pLineEnd = saved_char;

    return PARSE_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
PARSE_RESULT parse_headers
(
    REQUEST_INFO* ri
)
{
    if (!ri || !ri->m_pRawRequest) return ERR_EMPTY_REQUEST;
    if (!ri->m_pHeadersStart) return ERR_INVALID_FORMAT;

    /* mutable pointer into headers area */
    char* pHeadersStart = (char*)ri->m_pHeadersStart;
    size_t rem_len = remaining_from_pointer(ri, pHeadersStart);
    if (rem_len == 0) return ERR_INVALID_FORMAT;

    /* find end of headers: \r\n\r\n */
    char* pHeadersEnd = find_substr_in_bounds(pHeadersStart, rem_len, "\r\n\r\n");
    if (!pHeadersEnd) return ERR_INVALID_FORMAT;

    size_t iOffset = (size_t)(pHeadersEnd - pHeadersStart) + 4;
    if (iOffset > rem_len) return ERR_INVALID_FORMAT;

    ri->m_pBodyStart = (const char*)(pHeadersEnd + 4);

    /* Null-terminate header block for easy line parsing */
    char saved_char = *pHeadersEnd;
    *pHeadersEnd = '\0';

    char* pCurrentLine = pHeadersStart;
    HEADERS* h_entries = &ri->m_headers;

    /* If caller left headers uninitialized, parser will allocate them and take ownership */
    if (!h_entries->entries || h_entries->capacity == 0)
    {
        size_t init_capacity = 16;
        h_entries->entries = calloc(init_capacity, sizeof(HEADER_KEY_VALUE));
        if (!h_entries->entries)
        {
            *pHeadersEnd = saved_char;
            return ERR_CALLOC_FAILED;
        }
        h_entries->capacity = init_capacity;
        h_entries->count = 0;
    }

    while (pCurrentLine && *pCurrentLine != '\0')
    {
        /* ensure capacity */
        if (h_entries->count >= h_entries->capacity)
        {
            size_t old_cap = h_entries->capacity;
            size_t new_cap = old_cap ? old_cap * 2 : 16;
            HEADER_KEY_VALUE* pTemp = realloc(h_entries->entries, new_cap * sizeof(HEADER_KEY_VALUE));
            if (!pTemp)
            {
                *pHeadersEnd = saved_char;
                return ERR_CALLOC_FAILED;
            }
            /* zero the added portion */
            if (new_cap > old_cap)
            {
                size_t added = new_cap - old_cap;
                memset(pTemp + old_cap, 0, added * sizeof(HEADER_KEY_VALUE));
            }
            h_entries->entries = pTemp;
            h_entries->capacity = new_cap;
        }

        /* find end of this header line */
        char* pLineEnd = find_substr_in_bounds(pCurrentLine, remaining_from_pointer(ri, pCurrentLine), "\r\n");
        if (!pLineEnd) break;

        /* isolate header line */
        char saved_line_char = *pLineEnd;
        *pLineEnd = '\0';

        /* find colon */
        char* pColon = strchr(pCurrentLine, ':');
        if (!pColon)
        {
            /* restore and stop parsing headers */
            *pLineEnd = saved_line_char;
            break;
        }

        *pColon = '\0';
        h_entries->entries[h_entries->count].szKey = pCurrentLine;

        char* pValue = pColon + 1;
        while (*pValue == ' ') pValue++;

        h_entries->entries[h_entries->count].szValue = pValue;
        h_entries->count++;

        /* restore header line terminator just in case (not strictly necessary) */
        *pLineEnd = saved_line_char;

        /* advance to next line */
        pCurrentLine = pLineEnd + 2;
    }

    /* restore original header end character (should be '\r') */
    *pHeadersEnd = saved_char;

    return PARSE_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
PARSE_RESULT parse_body
(
    REQUEST_INFO* ri
)
{
    /*
        This function actually does not parse the body but formats
        the bytes of the body / extract the bytes from the body with
        a different transfer encoding.
        The actual parsing depends on "Content-Type" header of the body
        and will be done later
    */

    if (!ri || !ri->m_pRawRequest) return ERR_NULL_CHECK_FAILED;
    if (!ri->m_pBodyStart) return ERR_INVALID_FORMAT;

    /* ensure headers array exists (parser created it if absent) */
    if (!ri->m_headers.entries) return ERR_INVALID_FORMAT;

    char* szContentLength = NULL;
    char* szTransferEncoding = NULL;

    for (size_t iX = 0; iX < ri->m_headers.count; iX++)
    {
        const char* key = ri->m_headers.entries[iX].szKey;
        const char* val = ri->m_headers.entries[iX].szValue;

        if (!key) continue;

        if (strcmp(key, "Content-Length") == 0)
        {
            szContentLength = (char*)val;
        }

        if (strcmp(key, "Transfer-Encoding") == 0)
        {
            szTransferEncoding = (char*)val;
        }
    }

    /*
        HTTP rule:
        - If Transfer-Encoding exists → ignore Content-Length
        - Else if Content-Length exists → use it
        - Else → no body
    */

    /* -------- Chunked body -------- */
    if (szTransferEncoding && strcmp(szTransferEncoding, "chunked") == 0)
    {
        ri->m_is_chunked = true;
        /* compute safe body_len based on remaining bytes */
        size_t body_len = remaining_from_pointer(ri, ri->m_pBodyStart);
        return decode_chunked_body(ri, ri->m_pBodyStart, body_len);
    }

    /* -------- Content-Length body -------- */
    if (szContentLength)
    {
        size_t iBodySize = (size_t)atoi(szContentLength);

        ri->m_iBodyLength = iBodySize;
        ri->m_szBody = ri->m_pBodyStart;
        ri->m_pRequestEnd = ri->m_pBodyStart + iBodySize;

        return PARSE_SUCCESS;
    }

    /* -------- No body -------- */
    ri->m_iBodyLength = 0;
    ri->m_szBody = NULL;
    ri->m_pRequestEnd = ri->m_pBodyStart;

    return PARSE_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
PARSE_RESULT decode_chunked_body
(
    REQUEST_INFO* ri,
    const char*   body_start,
    size_t        body_len
)
{
    if (!ri || !body_start) return ERR_NULL_CHECK_FAILED;

    /* ensure body_start lies within raw request */
    size_t rem = remaining_from_pointer(ri, body_start);
    if (rem == 0) return ERR_INVALID_FORMAT;
    /* use min(rem, body_len) as effective end */
    size_t effective_len = body_len ? (body_len <= rem ? body_len : rem) : rem;

    char* pCurrentByte = (char*)body_start;
    char* pEnd = (char*)body_start + effective_len;

    size_t iCount = 0;
    size_t iCapacity = 1024;
    char* pBodyBuffer = calloc(iCapacity, 1);
    if (!pBodyBuffer) return ERR_CALLOC_FAILED;

    size_t iChunkSize = 0;

    do
    {
        iChunkSize = 0;
        int iHexChars = 0;

        /* parse hex digits until CR */
        while ((pCurrentByte + 1 < pEnd) && *pCurrentByte != '\r')
        {
            if (++iHexChars > 16) goto safe_return; /* too many hex digits */

            if (*pCurrentByte >= '0' && *pCurrentByte <= '9')
                iChunkSize = 16 * iChunkSize + (*pCurrentByte - '0');
            else if (*pCurrentByte >= 'A' && *pCurrentByte <= 'F')
                iChunkSize = 16 * iChunkSize + (10 + (*pCurrentByte - 'A'));
            else if (*pCurrentByte >= 'a' && *pCurrentByte <= 'f')
                iChunkSize = 16 * iChunkSize + (10 + (*pCurrentByte - 'a'));
            else
                goto safe_return;

            pCurrentByte++;
        }

        /* ensure we have space for CRLF after size */
        if (pCurrentByte + 1 >= pEnd) goto safe_return;

        /* if chunk size is 0 -> end of chunks (we'll handle trailers next) */
        if (iChunkSize == 0) break;

        /* guard overall size (simple safety limit) */
        if (iCount + iChunkSize > 0x00A00000ULL) goto safe_return;

        /* ensure capacity */
        while (!(iCount + iChunkSize < iCapacity))
        {
            iCapacity *= 2;
            char* pTempBuffer = realloc(pBodyBuffer, iCapacity);
            if (!pTempBuffer) { free(pBodyBuffer); return ERR_CALLOC_FAILED; }
            pBodyBuffer = pTempBuffer;
        }

        /* verify CRLF after hex size */
        if (!(*pCurrentByte == '\r' && *(pCurrentByte + 1) == '\n')) goto safe_return;
        pCurrentByte += 2;

        /* check that the full chunk + trailing CRLF are within bounds */
        if (pCurrentByte + iChunkSize + 2 > pEnd) goto safe_return;

        /* copy chunk bytes */
        for (size_t iX = 0; iX < iChunkSize; iX++)
            pBodyBuffer[iCount++] = *(pCurrentByte++);

        /* mandatory CRLF after chunk data */
        if (!(*pCurrentByte == '\r' && *(pCurrentByte + 1) == '\n')) goto safe_return;
        pCurrentByte += 2;

    } while (iChunkSize != 0);

    /* after loop, ensure we have at least final CRLF for 0 chunk */
    if (pCurrentByte + 1 >= pEnd) goto safe_return;

    /* No trailers: immediate empty line */
    if (*pCurrentByte == '\r' && *(pCurrentByte + 1) == '\n')
    {
        pCurrentByte += 2;
        ri->m_pRequestEnd = pCurrentByte;
    }
    else /* trailers present */
    {
        while (1)
        {
            if (pCurrentByte + 1 >= pEnd) goto safe_return;

            /* empty line marks end of trailers */
            if (*pCurrentByte == '\r' && *(pCurrentByte + 1) == '\n')
            {
                pCurrentByte += 2;
                break;
            }

            /* skip current trailer line until CRLF */
            while (pCurrentByte + 1 < pEnd &&
                   !(*pCurrentByte == '\r' && *(pCurrentByte + 1) == '\n'))
            {
                pCurrentByte++;
            }

            if (pCurrentByte + 1 >= pEnd) goto safe_return;

            /* consume CRLF */
            pCurrentByte += 2;
        }

        ri->m_pRequestEnd = pCurrentByte;
    }

    /* shrink to fit and null-terminate for convenience */
    char* pTemp = realloc(pBodyBuffer, iCount + 1);
    if (!pTemp) goto safe_return;
    pTemp[iCount] = '\0';
    pBodyBuffer = pTemp;

    ri->m_iBodyLength = iCount;
    ri->m_szBody = pBodyBuffer;
    ri->m_body_is_heap_allocated = true;

    return PARSE_SUCCESS;

safe_return:
    if (pBodyBuffer) free(pBodyBuffer);
    return ERR_INVALID_FORMAT;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//======================================= HELPER FUNCTIONS ======================================//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void free_request_info
(
    REQUEST_INFO* ri
)
{
    if (!ri) return;

    if (ri->m_headers.entries)
    {
        free(ri->m_headers.entries);
        ri->m_headers.entries = NULL;
    }
    ri->m_headers.count = 0;
    ri->m_headers.capacity = 0;

    if (ri->m_trailerHeaders.entries)
    {
        free(ri->m_trailerHeaders.entries);
        ri->m_trailerHeaders.entries = NULL;
    }
    ri->m_trailerHeaders.count = 0;
    ri->m_trailerHeaders.capacity = 0;

    if (ri->m_body_is_heap_allocated && ri->m_szBody)
    {
        free((void*)ri->m_szBody);
        ri->m_szBody = NULL;
    }
    ri->m_iBodyLength = 0;
    ri->m_body_is_heap_allocated = false;
    ri->m_is_chunked = false;

    /* NOTE: do NOT free ri->m_pRawRequest here — caller owns it */
    ri->m_pRequestStart = NULL;
    ri->m_pHeadersStart = NULL;
    ri->m_pBodyStart = NULL;
    ri->m_pRequestEnd = NULL;

    ri->m_szMethod = NULL;
    ri->m_szPath = NULL;
    ri->m_szVersion = NULL;

    /* reset parse result to safe default */
    ri->m_parseResult = PARSE_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void print_request_info
(
    const REQUEST_INFO* ri
)
{
    if (!ri)
    {
        printf("REQUEST_INFO is NULL\n");
        return;
    }

    printf("The Request Line elements are:\n");
    printf("Method: %s\n",  ri->m_szMethod  ? ri->m_szMethod  : "string is NULL");
    printf("Path: %s\n",    ri->m_szPath    ? ri->m_szPath    : "string is NULL");
    printf("Protocol: %s\n",ri->m_szVersion ? ri->m_szVersion : "string is NULL");

    printf("\nHeaders section:\n");

    if (!ri->m_headers.entries)
    {
        printf("Headers: NULL\n");
    }
    else
    {
        HEADER_KEY_VALUE* arrEntries = ri->m_headers.entries;

        for (size_t iX = 0; iX < ri->m_headers.count; iX++)
        {
            printf("%s: %s\n",
                arrEntries[iX].szKey   ? arrEntries[iX].szKey   : "string is NULL",
                arrEntries[iX].szValue ? arrEntries[iX].szValue : "string is NULL");
        }
    }

    printf("\nBody:\n");

    if (!ri->m_szBody)
    {
        printf("Body pointer is NULL\n");
    }
    else if (ri->m_iBodyLength == 0)
    {
        printf("(empty body)\n");
    }
    else
    {
        for (size_t iX = 0; iX < ri->m_iBodyLength; iX++)
        {
            putchar(ri->m_szBody[iX]);
        }
        putchar('\n');
    }

    printf("\n");
}
