/*
    File name: http.c
    Creation date: 10-12-25
    Author: Solomon
*/

#define _POSIX_C_SOURCE 200809L // for strtok_r which is a POSIX function not standard function

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include "http.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//======================================== MAIN PARSER API ========================================//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
PARSE_RESULT launch_parser(REQUEST_INFO* ri_requestInfo, char* pBytestream)
{
    if (!ri_requestInfo || !pBytestream)
        return ERR_NULL_CHECK_FAILED;

    if (parse_request_line(ri_requestInfo, pBytestream) != PARSE_SUCCESS)
        return ERR_REQUEST_LINE_PARSE_FAILED;

    if (parse_headers(ri_requestInfo, pBytestream) != PARSE_SUCCESS)
        return ERR_HEADERS_PARSE_FAILED;

    if (parse_body(ri_requestInfo, pBytestream) != PARSE_SUCCESS)
        return ERR_BODY_PARSE_FAILED;

    print_request_info(ri_requestInfo);

    return PARSE_SUCCESS;
}
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
PARSE_RESULT parse_request_line(REQUEST_INFO* ri_requestInfo, char* szRequestBuffer)
{ 
    if (!ri_requestInfo || !szRequestBuffer) return ERR_EMPTY_REQUEST;

    ri_requestInfo->m_pRequestStart = szRequestBuffer; // set the pointers in struct

    size_t iReqLength = strlen(szRequestBuffer);
    // 1. Ensure we only look at the first line
    char* pLineEnd = strstr(szRequestBuffer, "\r\n");
    if(!pLineEnd) return ERR_NULL_CHECK_FAILED;

    size_t iOffset = pLineEnd - szRequestBuffer + 2;
    if (iOffset > iReqLength) return ERR_INVALID_FORMAT;

    ri_requestInfo->m_pHeadersStart = pLineEnd + 2; // set the members of the struct
    
    // Terminate the first line so strtok doesn't bleed into headers
    *pLineEnd = '\0';

    char* pSavePtr;

    ri_requestInfo->m_szMethod = strtok_r(szRequestBuffer, " ", &pSavePtr);
    ri_requestInfo->m_szPath = strtok_r(NULL, " ", &pSavePtr);
    ri_requestInfo->m_szVersion = strtok_r(NULL, " ", &pSavePtr);

    if (!ri_requestInfo->m_szMethod) return ERR_INVALID_METHOD;
    if (!ri_requestInfo->m_szPath) return ERR_INVALID_PATH;
    if (!ri_requestInfo->m_szVersion) return ERR_INVALID_PROTOCOL;

    // no space after version
    if (strtok_r(NULL, " ", &pSavePtr) != NULL) {
        return ERR_INVALID_FORMAT; // Too many spaces/parameters
    }

    *pLineEnd = '\r';

    return PARSE_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
PARSE_RESULT parse_headers(REQUEST_INFO* ri_requestInfo, char* szRequestBuffer)
{
    if (!ri_requestInfo || !szRequestBuffer) 
        return ERR_EMPTY_REQUEST;

    char* pHeadersStart = ri_requestInfo->m_pHeadersStart;
    if (!pHeadersStart) 
        return ERR_INVALID_FORMAT;

    char* pHeadersEnd = strstr(pHeadersStart, "\r\n\r\n");
    if (!pHeadersEnd) 
        return ERR_INVALID_FORMAT;

    size_t iLength = strlen(pHeadersStart);
    size_t iOffset = pHeadersEnd - pHeadersStart + 4;
    if (iOffset > iLength) 
        return ERR_INVALID_FORMAT;

    ri_requestInfo->m_pBodyStart = pHeadersEnd + 4;

    *pHeadersEnd = '\0';   // Seal the header block

    char* pCurrentLine = pHeadersStart;
    HEADERS* h_entries = ri_requestInfo->m_h_headers;

    while (pCurrentLine && *pCurrentLine != '\0')
    {
        if (h_entries->iCount >= h_entries->iCapacity) 
            return ERR_OUT_OF_BOUNDS;

        char* pLineEnd = strstr(pCurrentLine, "\r\n");
        if (!pLineEnd) 
            break;

        *pLineEnd = '\0';   // isolate one header line

        char* pColon = strchr(pCurrentLine, ':');
        if (!pColon) 
            break;

        *pColon = '\0';
        h_entries->hkv_arrEntries[h_entries->iCount].szKey = pCurrentLine;

        char* pValue = pColon + 1;
        while (*pValue == ' ') pValue++;

        h_entries->hkv_arrEntries[h_entries->iCount].szValue = pValue;
        h_entries->iCount++;

        pCurrentLine = pLineEnd + 2;   // move to next header line
    }

    return PARSE_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
PARSE_RESULT parse_body(REQUEST_INFO* ri_requestInfo, char* szRequestBuffer)
{
    /*
        This function actually does not parse the body but formats 
        the bytes of the body / extract the bytes from the body with
        a different transfer encoding.
        The actual parsing depends on "Content-Type" header of the body
        and will be done later
    */

    if (!ri_requestInfo || !szRequestBuffer) 
        return ERR_NULL_CHECK_FAILED;

    if (!ri_requestInfo->m_pBodyStart) 
        return ERR_INVALID_FORMAT;

    if (!ri_requestInfo->m_h_headers) 
        return ERR_INVALID_FORMAT;

    char* szContentLength     = NULL;
    char* szTransferEncoding  = NULL;

    for (size_t iX = 0; iX < ri_requestInfo->m_h_headers->iCount; iX++)
    {
        if (strcmp(ri_requestInfo->m_h_headers->hkv_arrEntries[iX].szKey,
                   "Content-Length") == 0)
        {
            szContentLength =
                ri_requestInfo->m_h_headers->hkv_arrEntries[iX].szValue;
        }

        if (strcmp(ri_requestInfo->m_h_headers->hkv_arrEntries[iX].szKey,
                   "Transfer-Encoding") == 0)
        {
            szTransferEncoding =
                ri_requestInfo->m_h_headers->hkv_arrEntries[iX].szValue;
        }
    }

    /*
        HTTP rule:
        - If Transfer-Encoding exists → ignore Content-Length
        - Else if Content-Length exists → use it
        - Else → no body
    */

    // -------- Chunked body --------
    if (szTransferEncoding &&
        strcmp(szTransferEncoding, "chunked") == 0)
    {
        return decode_chunked_body(
            ri_requestInfo,
            ri_requestInfo->m_pBodyStart
        );
    }

    // -------- Content-Length body --------
    if (szContentLength)
    {
        size_t iBodySize = (size_t)atoi(szContentLength);

        ri_requestInfo->m_iBodyLength  = iBodySize;
        ri_requestInfo->m_szBody       = ri_requestInfo->m_pBodyStart;
        ri_requestInfo->m_pRequestEnd  = ri_requestInfo->m_pBodyStart + iBodySize;

        return PARSE_SUCCESS;
    }

    // -------- No body --------
    ri_requestInfo->m_iBodyLength = 0;
    ri_requestInfo->m_szBody      = NULL;
    ri_requestInfo->m_pRequestEnd = ri_requestInfo->m_pBodyStart;

    return PARSE_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
PARSE_RESULT decode_chunked_body(REQUEST_INFO* ri_requestInfo, char* pBodyStart)
{
    if (!ri_requestInfo || !pBodyStart) return ERR_NULL_CHECK_FAILED;

    size_t iCount     = 0;
    size_t iCapacity  = 1024;
    char* pBodyBuffer = calloc(iCapacity, 1);
    char* pEnd        = ri_requestInfo->m_pOriginalRequest + ri_requestInfo->m_iTotalRawBytes;

    char* pCurrentByte = pBodyStart;
    size_t iChunkSize  = 0;
    do 
    {
        iChunkSize = 0;
        int iHexChars = 0;
        
        while ((pCurrentByte + 1 < pEnd) && *pCurrentByte != '\r' )
        {
            if (++iHexChars > 16) goto safe_return; // Max 16 hex chars for 64-bit size_t

            if (*pCurrentByte >= '0' && *pCurrentByte <= '9')
                iChunkSize = 16 * iChunkSize +  (*pCurrentByte - '0');
            else if (*pCurrentByte >= 'A' && *pCurrentByte <= 'F')
                iChunkSize = 16 * iChunkSize + (10 + (*pCurrentByte - 'A'));
            else if (*pCurrentByte >= 'a' && *pCurrentByte <=  'f')
                iChunkSize = 16 * iChunkSize + (10 + (*pCurrentByte - 'a'));
            else goto safe_return;

            pCurrentByte++;
        }

        // MODIFIED: Ensure we aren't at the very end of the buffer before checking CRLF
        if (pCurrentByte + 1 >= pEnd) goto safe_return;

        if (iChunkSize == 0) break;

        if (iCount + iChunkSize > 0x00A00000ULL) goto safe_return; 
        while (!(iCount + iChunkSize < iCapacity))
        {
            iCapacity *= 2;
            char* pTempBuffer = realloc(pBodyBuffer, iCapacity);
            if (!pTempBuffer) {free(pBodyBuffer); return ERR_NULL_CHECK_FAILED;};
            pBodyBuffer = pTempBuffer;
        }
        
        // Verify CRLF after the hex size
        if (!(*pCurrentByte == '\r' && *(pCurrentByte + 1) == '\n')) goto safe_return;
        pCurrentByte += 2;

        // We check for iChunkSize + 2 to account for the trailing \r\n
        if (pCurrentByte + iChunkSize + 2 > pEnd) goto safe_return;

        for (size_t iX = 0; iX < iChunkSize; iX++)
            pBodyBuffer[iCount++] = *(pCurrentByte++);
        
        // Verify the mandatory CRLF that must follow every data chunk
        if (!(*pCurrentByte == '\r' && *(pCurrentByte + 1) == '\n')) goto safe_return;
        pCurrentByte += 2;

    } while (iChunkSize != 0);

    // Correctly handle the final CRLF of the 0 chunk (0\r\n\r\n)
    if (pCurrentByte + 1 >= pEnd)
    goto safe_return;

    // No trailers: immediate empty line
    if (*pCurrentByte == '\r' && *(pCurrentByte + 1) == '\n')
    {
        pCurrentByte += 2;
        ri_requestInfo->m_pRequestEnd = pCurrentByte;
    }

    else // else executes if there are trailers behind body section
    {
        while (1)
        {
            // Need at least 2 bytes to check for CRLF
            if (pCurrentByte + 1 >= pEnd) goto safe_return;

            // Empty line marks end of trailers
            if (*pCurrentByte == '\r' && *(pCurrentByte + 1) == '\n')
            {
                pCurrentByte += 2;
                break;
            }

            // Skip current trailer line until CRLF
            while (pCurrentByte + 1 < pEnd &&
                   !(*pCurrentByte == '\r' && *(pCurrentByte + 1) == '\n'))
            {
                pCurrentByte++;
            }

            if (pCurrentByte + 1 >= pEnd) goto safe_return;

            // Consume CRLF of this trailer line
            pCurrentByte += 2;
        }

        ri_requestInfo->m_pRequestEnd = pCurrentByte;
    }


    char* pTemp = realloc(pBodyBuffer, iCount + 1); 
    if (!pTemp) goto safe_return;
    pTemp[iCount] = '\0';
    pBodyBuffer = pTemp;

    ri_requestInfo->m_iBodyLength = iCount;
    ri_requestInfo->m_szBody = pBodyBuffer;
   
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
void print_request_info(REQUEST_INFO* ri_requestInfo)
{
    if (!ri_requestInfo)
    {
        printf("REQUEST_INFO is NULL\n");
        return;
    }

    printf("The Request Line elements are:\n");
    printf("Method: %s\n",  ri_requestInfo->m_szMethod  ? ri_requestInfo->m_szMethod  : "string is NULL");
    printf("Path: %s\n",    ri_requestInfo->m_szPath    ? ri_requestInfo->m_szPath    : "string is NULL");
    printf("Protocol: %s\n",ri_requestInfo->m_szVersion ? ri_requestInfo->m_szVersion : "string is NULL");

    printf("\nHeaders section:\n");

    if (!ri_requestInfo->m_h_headers)
    {
        printf("Headers: NULL\n");
    }
    else
    {
        HEADER_KEY_VALUE* arrEntries = ri_requestInfo->m_h_headers->hkv_arrEntries;

        for (size_t iX = 0; iX < ri_requestInfo->m_h_headers->iCount; iX++)
        {
            printf("%s: %s\n",
                arrEntries[iX].szKey   ? arrEntries[iX].szKey   : "string is NULL",
                arrEntries[iX].szValue ? arrEntries[iX].szValue : "string is NULL");
        }
    }

    printf("\nBody:\n");

    if (!ri_requestInfo->m_szBody)
    {
        printf("Body pointer is NULL\n");
    }
    else if (ri_requestInfo->m_iBodyLength == 0)
    {
        printf("(empty body)\n");
    }
    else
    {
        for (size_t iX = 0; iX < ri_requestInfo->m_iBodyLength; iX++)
        {
            putchar(ri_requestInfo->m_szBody[iX]);
        }
        putchar('\n');
    }

    printf("\n");
}
