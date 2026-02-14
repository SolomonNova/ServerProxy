/*
    File name: server.c  
    Createad at: 06-12-2025
    Author: Solomon
*/

#include <errno.h>      // provides EINTR macro
#include "server.h"     // provides SERVER struct
#include <stdbool.h>
#include <signal.h>     // provides kill()
#include <sys/socket.h> // provides socket(), bind(), listen()
#include <stdio.h>      // provides perror()
#include <fcntl.h>      // provides fcntl()
#include <string.h>     // provides memset()                        
#include <unistd.h>     // provides fork(), getpid(), usleep()
#include <sys/types.h>  // provides pid_t
#include <sys/wait.h>   // provides waitpid()

extern volatile sig_atomic_t g_master_running;

/*===================================== Set up the Master ======================================*/

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
SERVER server_create
(
    int           iDomain,
    int           iService,
    int           iProtocol,
    unsigned long iInterface,
    int           iPort,
    int           iBacklog,
    int           iWorkerCount
)
{
    SERVER s_Server;
    memset(&s_Server, 0, sizeof(s_Server));

    s_Server.m_iDomain      = iDomain;
    s_Server.m_iService     = iService;
    s_Server.m_iProtocol    = iProtocol;
    s_Server.m_iInterface   = iInterface;
    s_Server.m_iPort        = iPort;
    s_Server.m_iBacklog     = iBacklog;
    s_Server.m_iWorkerCount = iWorkerCount;
    s_Server.m_iListenFd    = -1;

    return s_Server;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
int server_setup_listener(SERVER* s_pServer)
{
    s_pServer->m_iListenFd = socket(
        s_pServer->m_iDomain,
        s_pServer->m_iService,
        s_pServer->m_iProtocol
    );

    if (s_pServer->m_iListenFd < 0) return -1;

    // 1) Get the current flag of the socket
    int iFlags = fcntl(s_pServer->m_iListenFd, F_GETFL, 0);
    if (iFlags < 0) return -1;

    // 2) Add the non blocking Macro into the socket
    if (fcntl(s_pServer->m_iListenFd, F_SETFL, iFlags | O_NONBLOCK) < 0)
        return -1;

    // the server can bind to the same IP:port even if the previous connection 
    // on that port is in TIME_WAIT state.
    int iOpt = 1;
    if (setsockopt(
            s_pServer->m_iListenFd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &iOpt,
            sizeof(iOpt)) < 0)
        return -1;
    
    // bind the socket to an ip address and a port number
    if (bind(
            s_pServer->m_iListenFd,
            (struct sockaddr*)&s_pServer->m_si_address,
            sizeof(s_pServer->m_si_address)) < 0)
        return -1;

    // start listening to the socket for any incoming requests
    if (listen(
            s_pServer->m_iListenFd,
            s_pServer->m_iBacklog) < 0)
        return -1;

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
int server_spawn_workers(SERVER* s_pServer)
{
    if (!s_pServer) return -1;
    
    for (size_t iX = 0; iX < s_pServer->m_iWorkerCount; ++iX)
    {
        pid_t pid = fork();
        if (pid < 0) return -1;

        if (pid == 0)
        {
            worker_run(s_pServer);
            _exit(0);
        }

        s_pServer->m_arrWorkers[iX] = pid;
    }
    
    return 0;  
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
void server_master_loop(SERVER* s_pServer)
{
    if (!s_pServer) return;    
    
    s_pServer->m_bRunning = true;
    while (s_pServer->m_bRunning && g_master_running)
    {
       int iStatus = 0;
       pid_t iDeadPid;

       while ((iDeadPid = waitpid(-1, &iStatus, WNOHANG)) > 0)
       {
            for (size_t iX = 0; iX < s_pServer->m_iWorkerCount; ++iX)
            {
                if (s_pServer->m_arrWorkers[iX] == iDeadPid)
                {
                    pid_t pid = fork();
                    if (pid == 0)
                    {
                        worker_run(s_pServer);
                        _exit(0);
                    }
                    s_pServer->m_arrWorkers[iX] = pid;
                    break;
                }
            }
       }

       usleep(200 * 1000);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
void server_shutdown(SERVER* s_pServer)
{
    if (!s_pServer) return;

    s_pServer->m_bRunning = false;

    for (size_t iX = 0; iX < s_pServer->m_iWorkerCount; iX++)
    {
        pid_t pid = s_pServer->m_arrWorkers[iX];
        if (pid > 0) kill(pid, SIGTERM);
    }

    for (size_t iX = 0; iX < s_pServer->m_iWorkerCount; ++iX)
    {
        pid_t pid = s_pServer->m_arrWorkers[iX];
        if (pid <= 0) continue;

        int iStatus;
        // 0 in waitpid() means block until a child exits
        while (waitpid(pid, &iStatus, 0) == -1)
        {
            if (errno != EINTR) break;
        }
    }

    if (s_pServer->m_iListenFd >= 0)
        close(s_pServer->m_iListenFd);
}
