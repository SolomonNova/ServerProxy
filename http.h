/*
    File name: http.h
    Purpose  : HTTP parser public interface and ownership contract
    Author   : Solomon
    Date     : 2026-02-22
*/

#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <stddef.h>   // provides size_t
#include <stdbool.h>  // provides bool type

/*
 * Ownership contract (short):
 * - Worker (caller) OWNS the raw receive buffer and is responsible for freeing it.
 * - Parser BORROWS pointers into the raw buffer (const char*) for method/path/version/headers/body
 *   whenever possible (zero-copy).
 * - Parser ALLOCATES the dynamic arrays (HEADERS.entries) and WILL ALLOCATE the decoded body
 *   for Transfer-Encoding: chunked. Those allocations are owned by REQUEST_INFO and must be
 *   freed by free_request_info().
 *
 * Concrete rules:
 * - Fields typed `const char *` point into ri->m_pRawRequest (non-owning). Do NOT free them.
 * - HEADERS.entries is heap-allocated (owning) and must be freed by free_request_info().
 * - If ri->m_body_is_heap_allocated is true, ri->m_szBody is heap memory that must be freed.
 * - Parser NEVER frees the raw buffer passed by the worker.
 */

/* ---------------- Parse result codes ---------------- */

typedef enum {
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
    ERR_UNSUPPORTED_TRANSFER_ENCODING,
} PARSE_RESULT;

/* ---------------- Headers / Key-Value ---------------- */

typedef struct HEADER_KEY_VALUE
{
    const char* szKey;    /* borrowed pointer into raw request (zero-copy) */
    const char* szValue;  /* borrowed pointer into raw request (zero-copy) */
} HEADER_KEY_VALUE;

typedef struct HEADERS
{
    HEADER_KEY_VALUE* entries; /* heap-allocated array (owned by REQUEST_INFO) */
    size_t            count;
    size_t            capacity;
} HEADERS;

/* ---------------- REQUEST_INFO (holds parsed view & ownership flags) ---------------- */

typedef struct REQUEST_INFO {
    /* Raw request buffer (owned by worker). Parser only borrows from this. */
    const char* m_pRawRequest;
    size_t      m_iTotalRawBytes; /* length of raw buffer in bytes */

    /* Request-line (borrowed views into m_pRawRequest). Do NOT free. */
    const char* m_szMethod;   /* e.g. "GET" */
    const char* m_szPath;     /* e.g. "/index.html" */
    const char* m_szVersion;  /* e.g. "HTTP/1.1" */

    /* Parsed headers */
    HEADERS     m_headers;           /* header entries array is allocated by parser */
    HEADERS     m_trailerHeaders;    /* trailer headers (if any), same ownership rules */

    /* Body:
     * - For Content-Length or no body: m_szBody points into raw buffer (borrowed).
     * - For chunked bodies: parser decodes into heap buffer, sets m_body_is_heap_allocated = true.
     */
    const char* m_szBody;
    size_t      m_iBodyLength;

    /* Pointers that mark boundaries (all borrowed into m_pRawRequest) */
    const char* m_pRequestStart;   /* usually == m_pRawRequest */
    const char* m_pHeadersStart;
    const char* m_pBodyStart;
    const char* m_pRequestEnd;

    /* Flags describing ownership / body form */
    bool m_is_chunked;               /* Transfer-Encoding: chunked was present */
    bool m_body_is_heap_allocated;    /* true if m_szBody was heap-allocated (decoded chunked body) */

    // status of the http request
    PARSE_RESULT m_parseResult;
} REQUEST_INFO;

/* ===================== Public API ===================== */

/*
 * launch_parser:
 *  - ri must be a pointer to REQUEST_INFO allocated by caller (worker).
 *  - pBytestream is the worker-owned receive buffer (must remain valid while ri is used).
 *  - bytestream_len is the size of the buffer in bytes.
 *  - On success: ri is populated with borrowed pointers into pBytestream and with any
 *    allocations required (headers.entries, and possibly decoded body).
 *
 * Ownership expectations after return:
 *  - Caller (worker) must call free_request_info(&ri) before freeing pBytestream.
 */
PARSE_RESULT launch_parser(REQUEST_INFO* ri, const char* pBytestream, size_t bytestream_len);

/* Lower-level parse functions which operate on ri->m_pRawRequest.
 * They are exposed for testing or incremental parsing if needed.
 * They expect ri->m_pRawRequest and m_iTotalRawBytes to be set before call.
 */
PARSE_RESULT parse_request_line(REQUEST_INFO* ri);
PARSE_RESULT parse_headers     (REQUEST_INFO* ri);
PARSE_RESULT parse_body        (REQUEST_INFO* ri);

/* decode_chunked_body:
 * - Decodes a chunked-encoded body starting at body_start with length body_len.
 * - On success it allocates a heap buffer, assigns it to ri->m_szBody and sets
 *   ri->m_iBodyLength and ri->m_body_is_heap_allocated = true.
 *
 * Note: body_start and body_len are used for safety; do not pass a non-bounded pointer.
 */
PARSE_RESULT decode_chunked_body(REQUEST_INFO *ri, const char *body_start, size_t body_len);

/* free_request_info:
 * - Frees any heap allocations made by the parser and resets fields to safe defaults.
 * - DOES NOT free ri itself and DOES NOT free the worker-owned raw buffer (m_pRawRequest).
 *
 * Implementations must:
 *   if (ri->m_headers.entries) free(ri->m_headers.entries);
 *   if (ri->m_trailerHeaders.entries) free(ri->m_trailerHeaders.entries);
 *   if (ri->m_body_is_heap_allocated && ri->m_szBody) free((void*)ri->m_szBody);
 *
 * After calling free_request_info, it's safe for the worker to free the raw buffer.
 */
void free_request_info(REQUEST_INFO *ri);

/* Utility for debugging (optional): prints a compact view of REQUEST_INFO.
 * Should not be used in production hot path unless needed for diagnostics.
 */
void print_request_info(const REQUEST_INFO *ri);

#endif /* HTTP_PARSER_H */
