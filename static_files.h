/*
    File name: static_files.h
    Created at: 18-12-25
    Author: Solomon
*/

#ifndef STATIC_FILES_H
#define STATIC_FILES_H

#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>

/*
    Holds metadata and permission information about a file
    Extracted from stat()
*/
typedef struct FileStats
{
    bool isRegular;     // true if regular file
    bool isDirectory;   // true if directory
    bool isSymLink;     // true if symbolic link

    bool canRead;       // readable by current process
    bool canWrite;      // writable by current process
    bool canExecute;    // executable by current process

    off_t size;         // file size in bytes
    uid_t owner;        // owner user ID
    gid_t group;        // owner group ID
} FileStats;

/*
    Converts a requested URL path into a filesystem path
    Example: "/" -> "./www/index.html"
*/
char* URLToFilePath(const char* URL);

/*
    Retrieves file metadata and permission flags for a given path
    Returns a populated FileStats structure
    Caller must check fields like isRegular / isDirectory
*/
FileStats getFileStats(const char* filePath);

void printFileStats(const FileStats* fs);

/*
    Returns the MIME type based on file extension
    Example: ".html" -> "text/html"
*/
const char* getMIMEType(const char* filePath);

/*
    Validates the requested URL path to prevent directory traversal
    Blocks "../", "//", and URL-encoded traversal attempts
*/
bool isSafePath(const char* URL);
char* normalizePath(char* szPath);
bool isHex(char c);

/*
    Checks file existence, type, and permissions using stat()
    Writes stat info into outStat
    Returns HTTP-style status code (200, 403, 404)
*/
int validateFileAccess(const char* filePath, struct stat* outStat);

/*
    Opens a file in read-only mode using POSIX open()
    Ensures binary-safe access for all file types
*/
int openFileReadOnly(const char* filePath);

/*
    Streams a file to a socket using a read/send loop
    Handles partial writes and avoids loading the entire file into memory
*/
bool sendFileToSocket(int socketFd, int fileFd, off_t fileSize);

/*
    Sends a minimal HTTP error response (403, 404, 500, etc.)
*/
void sendErrorResponse(int socketFd, int statusCode);
const char* getReasonPhrase(int statusCode); // phrase corrosponding to statusCode 
/*
    Determines whether a MIME type should be treated as text
    Used for charset and caching decisions
*/
bool isTextFile(const char* mimeType);

/*
    Logs request details for debugging or analytics
    Includes method, path, status code, and bytes sent
*/
void logRequest(const char* method, const char* path, int status, off_t bytesSent);

/*
    Safely closes file descriptors after transfer
    Prevents FD leaks in forked or concurrent servers
*/
void cleanupFileTransfer(int fileFd);

#endif
