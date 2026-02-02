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

/*===================================== Set up the Master ======================================*/

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
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

    return s_Server;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
int server_setup_listener(SERVER* s_pServer)
{
    // set up the socket
    s_pServer->m_iListenFd = socket
                             (
                                s_pServer->m_iDomain,
                                s_pServer->m_iService,
                                s_pServer->m_iProtocol
                             );

    if (s_pServer->m_iListenFd < 0) return -1;

    // set socket to NON-BLOCKING
    int iFlags = fcntl(s_pServer->m_iListenFd, F_GETFL, 0);              // F_GETFL -> Give me the current file status flags for this fd
    if (iFlags < 0) return -1;

    if (fcntl(s_pServer->m_iListenFd, F_SETFL, iFlags | O_NONBLOCK) < 0) // F_SETFL -> Set file status flags to this value.
            return -1;                                                   // iFlags | O_NONBLOCK -> adds the non block bit to the current flag
    
    // allows rebinding to the same port immediately after restart
    // SO_REUSEADDR means that the address can be re-used
    int iOpt = 1;
    if
    (
        setsockopt(s_pServer->m_iListenFd, SOL_SOCKET, SO_REUSEADDR, &iOpt, sizeof(iOpt)) < 0
    )
    {
        perror("setsockopt(SO_REUSEADDR) failed");
        return -1;
    }

    // bind
    if
    (
        bind
        (
            s_pServer->m_iListenFd,
            (struct sockaddr*)&s_pServer->m_si_address,
            sizeof(s_pServer->m_si_address)
        ) < 0
    )
    {
        perror("bind failed");
        return -1;
    }

    // listen
    if
    (
        listen
        (
            s_pServer->m_iListenFd,
            s_pServer->m_iBacklog
        ) < 0
    )
    {
        perror("listen failed");
        return -1;
    }

    return 0;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
int server_spawn_workers(SERVER* s_pServer)
{
    if (!s_pServer) return -1;
    
    for (size_t iX = 0; iX < s_pServer->m_iWorkerCount; ++iX)
    {
        pid_t pid = fork();
        if (pid < 0) { perror("fork failed"); return -1; };

        // child process
        if (pid == 0)
        {
            // worker process
            printf("[Worker %zu] started (PID = %d) \n", iX, getpid());
            worker_run(s_pServer);

            // worker_run should never return
            _exit(0);
        }

        // master process
        s_pServer->m_arrWorkers[iX] = pid;
        printf("[Master] spawned worker %zu (PID = %d)\n", iX, pid);
    }
    
    return 0;  
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void server_master_loop(SERVER* s_pServer)
{
    /*
        while the server is running, waits for any worker to die and respawn them immediately,
        the dead workers are identified by waitpid()
    */

    if (!s_pServer) return;    
    
    s_pServer->m_bRunning = true;
    while (s_pServer->m_bRunning)
    {
       int iStatus = 0; // the reason for child process exiting can be accessed using this int with MACROS like WIFEXITE(iStatus) etc.  
       pid_t iDeadPid;  // stores the pid of the exited child

       // Reap all exited workers (important)
       while ((iDeadPid = waitpid(-1, &iStatus, WNOHANG)) > 0)
       {
            int iDeadIndex = -1;
            for (size_t iX = 0; iX < s_pServer->m_iWorkerCount; ++iX)
            {
                if (s_pServer->m_arrWorkers[iX] == iDeadPid)
                { iDeadIndex = iX; break; }
            }

            if (iDeadIndex != -1)
            {
                printf("[Master] worker %d died (PID = %d). Respawning...\n", iDeadIndex, iDeadPid);
                respawn_worker(s_pServer, iDeadIndex);
            }
       }

       usleep(200 * 1000); // let this thread sleep for 200ms
    }
}

///////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void server_shutdown(SERVER* s_pServer)
{
    /*
        sends a kill signal to all the worker processes.
        uses waitpid() to clear out the zombie processes after the singnal was sent
        closes the listening socket
    */
    if (!s_pServer) return;

    s_pServer->m_bRunning = false;

    // kill the workers
    for (size_t iX = 0; iX < s_pServer->m_iWorkerCount; iX++)
    {
        pid_t pid = s_pServer->m_arrWorkers[iX];
        if (pid > 0) kill(pid, SIGTERM);
    }

    // reap workser
    for (size_t iX = 0; iX < s_pServer->m_iWorkerCount; ++iX)
    {
        pid_t pid = s_pServer->m_arrWorkers[iX];
        if (pid <= 0) continue;

        int iStatus;
        while (waitpid(pid, &iStatus, 0) == -1)
        {
            if (errno != EINTR) break;
        }
    }

    // close listening socket
    if (s_pServer->m_iListenFd >= 0)
    {
        close(s_pServer->m_iListenFd);
        s_pServer->m_iListenFd = -1;
    }
}

/*============================== Worker Control (called by master) ==============================*/

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
pid_t spawn_single_worker(SERVER *s_pServer)
{
    if (!s_pServer) return -1;

    pid_t pid = fork();
    if (pid < 0) { perror("fork failed"); return -1; }

    // child process
    if (pid == 0)
    {
        // worker should not run master logic
        // worker enters infinite event loop

        worker_run(s_pServer);

        // worker_run must never return;
        _exit(0);
    }

    return pid;
}


//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void respawn_worker(SERVER* s_pServer, int iIndex)
{
    if (!s_pServer) return;
    if (iIndex < 0 || iIndex >= s_pServer->m_iWorkerCount) return;

    pid_t pid = spawn_single_worker(s_pServer);
    if (pid > 0)
    {
        s_pServer->m_arrWorkers[iIndex] = pid;
        printf("[Master] respawned worker %d (PID = %d)\n", iIndex, pid);
    }
}
