/*
    File name    : worker.c
    creation date: 23-01-26
    Author       : Solomon
*/

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdint.h>
#include <errno.h>
#include "worker.h"
#include "server.h"
#include "http.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

static volatile sig_atomic_t g_Running = 1;

static void worker_on_signal(int sig)
{
    (void)sig;
    g_Running = 0;
}

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
    ev.events = EPOLLIN;
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

                const char body[] = "Hello World\n";
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
