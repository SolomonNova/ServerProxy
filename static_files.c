/*
    File name: static_files.c
    Created at: 18-12-25
    Author: Solomon
*/

#include <sys/socket.h>   // provoides send()
#include <fcntl.h>        // provides open(), O_RDONLY
#include <unistd.h>       // provides close(), read(), write()
#include <string.h>       // provides strcmp(), strlen(), strcpy(), strcat()
#include <stdio.h>        // provides printf()
#include <stdlib.h>       // provides free(), calloc()
#include <sys/stat.h>     // provides struct stat;
#include <stdbool.h>
#include <linux/limits.h> // provides PATH_MAX
#include <errno.h>        // provides errno
#include <inttypes.h>     // provides sszie_t
#include "static_files.h"
#include "data_structures/stack.h"

#define ROOT "./www"
#define ROOT_LENGTH 5

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
void serverFile(const char* szURL, int socketFd)
{
    /*
        Entry point for serving a static file over a socket
        Coordinates path validation, access checks, and streaming
    */

    if (!isSafePath(szURL)) return;

    char* szBuffer = URLToFilePath(szURL);
    if (!szBuffer) return;

    struct stat stStat;
    int iStatus = validateFileAccess(szBuffer, &stStat);
    if (iStatus != 200)
    {
        sendErrorResponse(socketFd, iStatus);
        free(szBuffer);
        return;
    }

    int iFileFd = openFileReadOnly(szBuffer);
    if (iFileFd < 0)
    {
        sendErrorResponse(socketFd, 500);
        free(szBuffer);
        return;
    }

    if (!sendFileToSocket(socketFd, iFileFd, stStat.st_size))
        sendErrorResponse(socketFd, 500);

    close(iFileFd);
    free(szBuffer);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
char* URLToFilePath(const char* szURL)
{
    /*
        Converts a URL path into a filesystem path under ROOT
        Dynamically allocates and resizes buffer as needed
    */

    if (strcmp(szURL, "/") == 0)
        szURL = "/index.html";

    size_t iRootLen = strlen(ROOT);
    size_t iURLLen  = strlen(szURL);
    size_t iBufferSize = 128;

    char* szBuffer = calloc(iBufferSize, 1);
    if (!szBuffer) return NULL;

    while (iRootLen + iURLLen + 1 > iBufferSize)
    {
        iBufferSize *= 2;
        char* szTemp = realloc(szBuffer, iBufferSize);
        if (!szTemp)
        {
            free(szBuffer);
            return NULL;
        }
        szBuffer = szTemp;
    }

    strcpy(szBuffer, ROOT);
    strcat(szBuffer, szURL);

    return szBuffer;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
FileStats getFileStats(const char* szFilePath)
{
    /*
        Collects metadata about a file using stat()
        Populates a FileStats structure with permission and type info
    */

    FileStats fsStats = {0};
    struct stat stStat;

    if (stat(szFilePath, &stStat) != 0)
        return fsStats;

    fsStats.isRegular   = S_ISREG(stStat.st_mode);
    fsStats.isDirectory = S_ISDIR(stStat.st_mode);
    fsStats.isSymLink   = S_ISLNK(stStat.st_mode);

    fsStats.canRead     = (stStat.st_mode & S_IRUSR) != 0;
    fsStats.canWrite    = (stStat.st_mode & S_IWUSR) != 0;
    fsStats.canExecute  = (stStat.st_mode & S_IXUSR) != 0;

    fsStats.size  = stStat.st_size;
    fsStats.owner = stStat.st_uid;
    fsStats.group = stStat.st_gid;

    return fsStats;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
void printFileStats(const FileStats* pStats)
{
    /*
        Debug helper to print file metadata to stdout
        Used during development and verification only
    */

    printf("File type:\n");
    printf("  Regular file : %s\n", pStats->isRegular   ? "yes" : "no");
    printf("  Directory    : %s\n", pStats->isDirectory ? "yes" : "no");
    printf("  Symbolic link: %s\n", pStats->isSymLink   ? "yes" : "no");

    printf("\nPermissions (owner):\n");
    printf("  Read    : %s\n", pStats->canRead    ? "yes" : "no");
    printf("  Write   : %s\n", pStats->canWrite   ? "yes" : "no");
    printf("  Execute : %s\n", pStats->canExecute ? "yes" : "no");

    printf("\nMetadata:\n");
    printf("  Size    : %ld bytes\n", (long)pStats->size);
    printf("  Owner   : UID %u\n", pStats->owner);
    printf("  Group   : GID %u\n", pStats->group);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
const char* getMIMEType(const char* szFilePath)
{
    /*
        Determines MIME type based on file extension
        Returns static string literals for HTTP headers
    */

    int iDotIndex = -1;
    int iLen = strlen(szFilePath);

    for (int iX = iLen - 1; iX >= 0; iX--)
    {
        if (szFilePath[iX] == '.')
        {
            iDotIndex = iX;
            break;
        }
    }

    if (iDotIndex == -1)
        return "application/octet-stream";

    int iExtLen = iLen - iDotIndex - 1;
    if (iExtLen <= 0)
        return "application/octet-stream";

    char* szExt = calloc(iExtLen + 1, 1);
    if (!szExt)
        return "application/octet-stream";

    memcpy(szExt, szFilePath + iDotIndex + 1, iExtLen);

    if (strcmp(szExt, "html") == 0 || strcmp(szExt, "htm") == 0)
    {
        free(szExt);
        return "text/html";
    }
    if (strcmp(szExt, "css") == 0)
    {
        free(szExt);
        return "text/css";
    }
    if (strcmp(szExt, "js") == 0)
    {
        free(szExt);
        return "application/javascript";
    }
    if (strcmp(szExt, "png") == 0)
    {
        free(szExt);
        return "image/png";
    }
    if (strcmp(szExt, "jpg") == 0 || strcmp(szExt, "jpeg") == 0)
    {
        free(szExt);
        return "image/jpeg";
    }
    if (strcmp(szExt, "gif") == 0)
    {
        free(szExt);
        return "image/gif";
    }
    if (strcmp(szExt, "svg") == 0)
    {
        free(szExt);
        return "image/svg+xml";
    }
    if (strcmp(szExt, "ico") == 0)
    {
        free(szExt);
        return "image/x-icon";
    }
    if (strcmp(szExt, "json") == 0)
    {
        free(szExt);
        return "application/json";
    }
    if (strcmp(szExt, "txt") == 0)
    {
        free(szExt);
        return "text/plain";
    }
    if (strcmp(szExt, "pdf") == 0)
    {
        free(szExt);
        return "application/pdf";
    }

    free(szExt);
    return "application/octet-stream";
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
bool isSafePath(const char* szURL)
{
    /*
        Validates and normalizes a URL path to prevent traversal
        Ensures resolved filesystem path stays within ROOT
    */

    if (!szURL || szURL[0] != '/') return false;

    size_t iLen = strlen(szURL);
    char* szDecoded = calloc(iLen + 1, 1);
    if (!szDecoded) return false;

    size_t iOut = 0;

    for (size_t iX = 0; iX < iLen; iX++)
    {
        if (szURL[iX] == '\\') { free(szDecoded); return false; }
        if ((unsigned char)szURL[iX] < 0x20 || szURL[iX] == 0x7F)
        {
            free(szDecoded);
            return false;
        }

        if (szURL[iX] == '%' && iX + 2 < iLen &&
            isHex(szURL[iX + 1]) && isHex(szURL[iX + 2]))
        {
            unsigned char iHi = isHex(szURL[iX + 1]);
            unsigned char iLo = isHex(szURL[iX + 2]);
            szDecoded[iOut++] = (iHi << 4) | iLo;
            iX += 2;
            continue;
        }

        if (szURL[iX] == '%') { free(szDecoded); return false; }
        szDecoded[iOut++] = szURL[iX];
    }

    szDecoded[iOut] = '\0';

    char* szNormalized = normalizePath(szDecoded);
    free(szDecoded);
    if (!szNormalized) return false;

    char* szFullPath = calloc(strlen(szNormalized) + ROOT_LENGTH + 1, 1);
    if (!szFullPath)
    {
        free(szNormalized);
        return false;
    }

    strcpy(szFullPath, ROOT);
    strcat(szFullPath, szNormalized);
    free(szNormalized);

    char szResolved[PATH_MAX];
    if (!realpath(szFullPath, szResolved))
    {
        free(szFullPath);
        return false;
    }

    static char szResolvedRoot[PATH_MAX];
    static bool bRootResolved = false;

    if (!bRootResolved)
    {
        if (!realpath(ROOT, szResolvedRoot))
        {
            free(szFullPath);
            return false;
        }
        bRootResolved = true;
    }

    size_t iRootLen = strlen(szResolvedRoot);
    bool bOk = strncmp(szResolved, szResolvedRoot, iRootLen) == 0 &&
               (szResolved[iRootLen] == '/' || szResolved[iRootLen] == '\0');

    free(szFullPath);
    return bOk;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
char* normalizePath(char* szPath)
{
    /*
        Normalizes path segments using a stack-based approach
        Resolves '.', '..' and rejects root escape attempts
    */

    Stack stStack;
    initialize(&stStack, 20, sizeof(char*));

    char* szToken = strtok(szPath, "/");
    while (szToken)
    {
        if (strcmp(szToken, ".") == 0)
        {
            szToken = strtok(NULL, "/");
            continue;
        }

        if (strcmp(szToken, "..") == 0)
        {
            if (isEmpty(&stStack))
                return NULL;
            pop(&stStack);
            szToken = strtok(NULL, "/");
            continue;
        }

        push(&stStack, szToken);
        szToken = strtok(NULL, "/");
    }

    char** pszItems = (char**)stStack.array;
    size_t iTotal = 1;

    for (size_t iX = 0; iX < stStack.top; iX++)
        iTotal += strlen(pszItems[iX]) + 1;

    char* szBuffer = calloc(iTotal + 1, 1);
    if (!szBuffer) return NULL;

    char* p = szBuffer;
    for (size_t iX = 0; iX < stStack.top; iX++)
    {
        *p++ = '/';
        size_t iLen = strlen(pszItems[iX]);
        memcpy(p, pszItems[iX], iLen);
        p += iLen;
    }

    return szBuffer;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
bool isHex(char c)
{
    /*
        Checks whether a character is a hexadecimal digit
        Used for validating percent-encoded URL sequences
    */

    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
int validateFileAccess(const char* szFilePath, struct stat* outStat)
{
    /*
        Validates file existence, type, and read permissions
        Maps POSIX errors to HTTP-style status codes
    */

    struct stat stFile;

    if (stat(szFilePath, &stFile) != 0)
    {
        switch (errno)
        {
            case ENOENT:
            case ENOTDIR: return 404;
            case EACCES:
            case EPERM:   return 403;
            case EINVAL:  return 400;
            case ENAMETOOLONG: return 414;
            default:      return 500;
        }
    }

    if (!S_ISREG(stFile.st_mode)) return 403;
    if (!(stFile.st_mode & S_IRUSR)) return 403;

    if (outStat) *outStat = stFile;
    return 200;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
int openFileReadOnly(const char* szFilePath)
{
    /*
        Opens a file descriptor in read-only mode
        Uses POSIX open for binary-safe file access
    */

    return open(szFilePath, O_RDONLY);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
bool sendFileToSocket(int socketFd, int fileFd, off_t fileSize)
{
    /*
        Streams file contents to a socket using buffered I/O
        Correctly handles partial writes and interruptions
    */

    char szBuffer[8192];
    off_t iTotalSent = 0;

    while (iTotalSent < fileSize)
    {
        ssize_t iBytesRead = read(fileFd, szBuffer, sizeof(szBuffer));
        if (iBytesRead <= 0) return false;

        ssize_t iSent = 0;
        while (iSent < iBytesRead)
        {
            ssize_t iN = send(socketFd, szBuffer + iSent,
                              iBytesRead - iSent, 0);
            if (iN <= 0) return false;

            iSent += iN;
            iTotalSent += iN;
        }
    }

    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
void sendErrorResponse(int socketFd, int statusCode)
{
    /*
        Sends a minimal HTTP error response to the client
        Includes status line, headers, and short body
    */

    const char* szReason = getReasonPhrase(statusCode);

    char szBody[64];
    int iBodyLength = snprintf(
        szBody, sizeof(szBody),
        "%d %s\n", statusCode, szReason
    );

    char szHeaders[256];
    int iHeaderLength = snprintf(
        szHeaders, sizeof(szHeaders),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        statusCode, szReason, iBodyLength
    );

    send(socketFd, szHeaders, iHeaderLength, 0);
    send(socketFd, szBody, iBodyLength, 0);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
bool isTextFile(const char* mimeType)
{
    /*
        Determines whether a MIME type should be treated as text
        Used to decide charset handling and caching rules
    */

    if (!mimeType) return false;

    return
    (
        strncmp(mimeType, "text/", 5) == 0 ||
        strcmp(mimeType, "application/json") == 0 ||
        strcmp(mimeType, "application/javascript") == 0 ||
        strcmp(mimeType, "application/xml") == 0
    );
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
const char* getReasonPhrase(int statusCode)
{
    /*
        Maps HTTP status codes to reason phrases
        Used when constructing error responses
    */

    switch (statusCode)
    {
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default:  return "Error";
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
void logRequest(const char* method, const char* path,
                int status, off_t bytesSent)
{
    /*
        Logs request and response details for diagnostics
        Includes method, path, status, and byte count
    */

    printf(
        "%s %s -> %d (%jd bytes)\n",
        method, path, status, (intmax_t)bytesSent
    );
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
void cleanupFileTransfer(int fileFd)
{
    /*
        Cleans up file descriptors after transfer
        Prevents resource leaks in long-running servers
    */

    if (fileFd >= 0)
        close(fileFd);
}
