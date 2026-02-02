 /*
    File name    : worker.c
    creation date: 23-01-26
    Author       : Solomon
*/

#define _GNU_SOURCE    // required for accept4() on Linux

#include <fcntl.h>     // provides fcntl() and F_GETFL and O_NONBLOCK
#include <stdint.h>    // provides unit32_t
#include <errno.h>     // provides errno macro
#include "worker.h"
#include "server.h"    // provides the struct SERVER
#include "http.h"      // provides REQUEST INFO struct
#include <sys/socket.h>// provides struct sockaddr_in
#include <netinet/in.h>// provdies socklen_t
#include <unistd.h>    // provides usleep()
#include <sys/epoll.h> // provides epoll_create1(), epoll_ctl(), epoll_wait(), struct epoll_event
#include <stdio.h>     // provides perror()
#include <string.h>
#include "http.h"
#include <signal.h>    // provides sig_atomic_t


static volatile sig_atomic_t g_Running = 1;
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
static void worker_on_signal(int sig)
{
    (void)sig;
    g_Running = 0;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void worker_run(struct SERVER* s_pServer)
{
    if (!s_pServer)                 return;
    if (s_pServer->m_iListenFd < 0) return;

    signal(SIGTERM, worker_on_signal);
    signal(SIGINT, worker_on_signal);
    
    int iEpollFd = -1;
    
    if (worker_init(&iEpollFd) < 0) { perror("worker_init failed"); return; }

    // register listening socket into epoll
    struct epoll_event ev;      // the associated fd is ready for reading
    memset(&ev, 0, sizeof(ev));

    ev.events  = EPOLLIN;
    ev.data.fd = s_pServer->m_iListenFd;
    
    if (epoll_ctl(iEpollFd, EPOLL_CTL_ADD, s_pServer->m_iListenFd, &ev) < 0)
    {
        perror("epoll_ctl ADD listen_fd failed");
        worker_cleanup(s_pServer, iEpollFd);
        return;
    }

    worker_event_loop(s_pServer, iEpollFd);
    worker_cleanup(s_pServer, iEpollFd);
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
int worker_init(int* pEpollFd)
{
    if (!pEpollFd) return -1;

    int iEpollFd = epoll_create1(EPOLL_CLOEXEC);
    if (iEpollFd < 0) { perror("epoll_create1 failed"); return -1; }

    *pEpollFd = iEpollFd;

    return 0;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void worker_event_loop(struct SERVER* s_pServer, int iEpollFd)
{
    if (!s_pServer)   return;
    if (iEpollFd < 0) return;
    
    const int iMaxEvents = 64;
    struct epoll_event events[iMaxEvents];

    while (g_Running) 
    {
        int iN = epoll_wait(iEpollFd, events, iMaxEvents, - 1);
        if (iN < 0)
        {
            if (errno == EINTR) continue;

            perror("epoll_wait failed");
            break;
        }

        for (int iX = 0; iX < iN; ++iX)
        {
            int iFd = events[iX].data.fd;
            uint32_t uEv = events[iX].events;

            // error or hangup on fd
            if (uEv & (EPOLLERR | EPOLLHUP))
            {
                epoll_ctl(iEpollFd, EPOLL_CTL_DEL, iFd, NULL);
                close(iFd);
                continue;
            }

            // listening socket -> accept new clients
            if (iFd == s_pServer->m_iListenFd)
                worker_accept_clients(s_pServer, iEpollFd);

            // client socket -> handle client I/O
            else if (uEv & EPOLLIN)
                worker_handle_client(iFd);              
        }
    }
        
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void worker_accept_clients(struct SERVER* s_pServer, int iEpollFd)
{
    if (!s_pServer)   return;
    if (iEpollFd < 0) return;

    while (g_Running)
    {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);

        int iClientFd = accept4
                        (
                         s_pServer->m_iListenFd,
                         (struct sockaddr*)&clientAddr,
                         &clientLen,
                         SOCK_NONBLOCK | SOCK_CLOEXEC
                        );

        if (iClientFd < 0)
        {
            // no more pending connections
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            
            // interruped -> retry
            if (errno == EINTR) continue;

            perror("accept4() failed");
            break;
        }

        // register client socket into epoll
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));

        ev.events = EPOLLIN | EPOLLRDHUP; // EPOLLIN -> there is data to read on the socket or the peer has performed normal close
        ev.data.fd = iClientFd;           // EPOLLRDHUP -> the peer has closed its write side of the connection (FIN received)

        if (epoll_ctl(iEpollFd, EPOLL_CTL_ADD, iClientFd, &ev) < 0)
        {
            perror("epoll_ctl ADD client_fd failed");
            close(iClientFd);
            continue;
        }
    }
}
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void worker_handle_client(int iClientFd)
{
    if (iClientFd < 0) return;
    
    uint32_t uBufferMax = 64000;
    char buffer[uBufferMax];

    int iBytesRead = recv(iClientFd, buffer, uBufferMax - 1, 0);
    if (iBytesRead < 0) 
    {
        // not data yet (non blockign socket)
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;

        perror("recv() falied");
        close(iClientFd);
        return;

    }
    
    // client closed connection
    if (iBytesRead == 0)
    {
        close(iClientFd);
        return;
    }

    buffer[iBytesRead] = '\0';

    REQUEST_INFO ri;
    memset(&ri, 0, sizeof(ri));

    if (launch_parser(&ri, buffer) != PARSE_SUCCESS)
    {
        // bad request
        close(iClientFd);
        return;
    }

    printf("body is: %s\n", ri.m_szBody);

    char response[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 12\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Hello World\n";    
    
    if (send(iClientFd, response, sizeof(response) - 1, 0) < 0)
    {
        perror("send() failed");
        return;
    }
}
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
int worker_set_nonblocking(int iFd)
{
    if (iFd < 0) return -1;

    int flags = fcntl(iFd, F_GETFL, 0);
    if (flags < 0)
    {
        perror("fcntl() failed");
        return -1;
    }

    if (fcntl(iFd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        perror("fcntl() F_SETFL");
        return -1;
    }
    
    return 0;
}
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void worker_cleanup(struct SERVER* s_pServer, int iEpollFd)
{
    if (!s_pServer)   return;
    if (iEpollFd < 0) return;

    s_pServer->m_bRunning = false;

    if (s_pServer->m_iListenFd >= 0)
        close(s_pServer->m_iListenFd); 
}
   
