/*
    File name    : worker.c
    creation date: 23-01-26
    Author       : Solomon
*/

#define _GNU_SOURCE     // enables GNU extension for accept4() and some flags like SOCK_NONBLOCK, SOCK_CLOEXEC

#include <fcntl.h>      // provides O_NONBLOCK O_CLOEXEC
#include <stdint.h>     // provides uint32_t
#include <errno.h>      // provides errno, EINTR
#include "worker.h"
#include <sys/socket.h> // provides accept4(), recv(), send(), struct sockaddr
#include <netinet/in.h> // provides IPv4 socket structures like struct sockaddr_in
#include <sys/epoll.h>  // provides epoll_create1(),  epoll_wait(), struct epoll_event, EPOLLIN, EPOLLERR, EPOLLHUP, EPOLLRDHUP
#include <stdio.h>      // provides snprintf()
#include <string.h>     // provides memset(), strlen()
#include <signal.h>     // signal(), SIGTERM, SIGINT, SIGPIPE, sig_atomic_t
#include "server.h"
#include "http.h"
#include "response.h"   // provides send_simple_response

static volatile sig_atomic_t g_Running = 1;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
static void worker_on_signal(int sig)
{
    (void)sig;
    g_Running = 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
void worker_run(struct SERVER* s_pServer)
{
    if (!s_pServer) return;
    if (s_pServer->m_iListenFd < 0) return;

    signal(SIGTERM, worker_on_signal);
    signal(SIGINT, worker_on_signal);
    signal(SIGPIPE, SIG_IGN);
    
    int iEpollFd = epoll_create1(EPOLL_CLOEXEC);
    if (iEpollFd < 0) return;

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN; // tells epoll that the socket has something to read
    ev.data.fd = s_pServer->m_iListenFd;

    if (epoll_ctl(iEpollFd, EPOLL_CTL_ADD, s_pServer->m_iListenFd, &ev) < 0)
        return;

    struct epoll_event events[64];

    while (g_Running)
    {
        int iN = epoll_wait(iEpollFd, events, 64, -1);
        if (iN < 0)
        {
            if (errno == EINTR) continue;
            break;
        }

        for (int iX = 0; iX < iN; ++iX)
        {
            int iFd = events[iX].data.fd;
            uint32_t uEv = events[iX].events;
            
            // if returned flag has any of the three
            // EPOLLERR -> socket has pending error
            // EPOLLHUP -> connection closed
            // EPOLLRDHUP -> peer performed shutdown
            if (uEv & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
            {
                epoll_ctl(iEpollFd, EPOLL_CTL_DEL, iFd, NULL);
                close(iFd);
                continue;
            }

            if (iFd == s_pServer->m_iListenFd)
            {
                struct sockaddr_in clientAddr;
                socklen_t clientLen = sizeof(clientAddr);

                int iClientFd = accept4(
                    s_pServer->m_iListenFd,
                    (struct sockaddr*)&clientAddr,
                    &clientLen,
                    SOCK_NONBLOCK | SOCK_CLOEXEC
                );
                
                // make an epoll instance of the client
                // EPOLLIN -> notify when client sends data
                // EPOLLRDHUP -> notify when clients closes its read / write or finished sending the request
                if (iClientFd >= 0)
                {
                    struct epoll_event cev;
                    memset(&cev, 0, sizeof(cev));
                    cev.events = EPOLLIN | EPOLLRDHUP;
                    cev.data.fd = iClientFd;
                    epoll_ctl(iEpollFd, EPOLL_CTL_ADD, iClientFd, &cev);
                }
            }
            else if (uEv & EPOLLIN)
            {
                char buffer[64000];
                int n = recv(iFd, buffer, sizeof(buffer) - 1, 0);
                if (n <= 0)
                {
                    epoll_ctl(iEpollFd, EPOLL_CTL_DEL, iFd, NULL);
                    close(iFd);
                    continue;
                }

                buffer[n] = '\0';

                /* --------------- parse request and print to terminal --------------- */
                REQUEST_INFO ri = { 0 };

                PARSE_RESULT rc = launch_parser(&ri, buffer, n);
                if (rc != PARSE_SUCCESS)
                {
                    send_parse_error_response(iFd, &ri);
                    free_request_info(&ri);
                    return;
                }

                /* here normal request handling begins */
                handle_application_request(iFd, &ri);
                free_request_info(&ri);

                /* ------------------------------------------------------------------ */

                const char body[] = "test successful";
                char response[256];

                snprintf(response, sizeof(response),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: %zu\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "%s",
                    strlen(body), body
                );

                send(iFd, response, strlen(response), 0);
                epoll_ctl(iEpollFd, EPOLL_CTL_DEL, iFd, NULL);
                close(iFd);
            }
        }
    }

    close(iEpollFd);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
void handle_application_request(int iClientFd, const REQUEST_INFO *ri)
{
    /* Only GET method is suppored */
    if (strcmp(ri->m_szMethod, "GET") != 0)
    {
        send_simple_response(iClientFd, ri, 405, "Method Not Allowed", NULL, 0);
        return;
    }

    if (strcmp(ri->m_szPath, "/") == 0)
    {
        const char body[] = "Hello, world\n";
        send_simple_response(
            iClientFd,
            ri,
            200,
            "OK",
            body,
            sizeof(body) - 1
        );
        return;
    }

    /* default */
    send_simple_response(iClientFd, ri, 404, "Not Found", NULL, 0);
}
