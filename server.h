/*
    File name: server.h
    Created at: 06-12-2025
    Author: Solomon
*/

#ifndef SERVER_H
#define SERVER_H

#include <sys/types.h>  // pid_t (typedef of an int for a process id)
#include <netinet/in.h> // struct sockaddr_in
#include <stdbool.h>
#include "worker.h"

#define MAX_WORKERS 32

/*
* @brief Represents a server configuration
* 
* @param - domain      Address family (AF_INET, AF_INET6)    -> tells which ip version to use in the server.

* @param - service     Socket type (SOCK_STREAM, SOCK_DGRAM) -> SOCK_STREAM type socket is always used with TCP, SOCK_DGRAM is always used with UDP.

* @param - protocol    Protocol (IPPROTO_TCP, IPPROTO_UDP)   -> protocol to be used, either TCP or UDP.

* @param - interface   Interface Address (INADDR_ANY)         -> tells which network interface in this system must the server be bound to.

* @param - port        Port number                           -> Identifies unique service bounded to this number, there can be many ports in a system
                                                                each running a different.

* @param - address     struct sockaddr_in type address       -> Tells the OS, which address family (IPv4) server belongs to,
                                                                port number being which the server will listen to,
                                                                IP address of the network interface that the port is attached on.

* @param - backlog     backlog number                        -> is the number of pending connections the kernel will queue after listen()
                                                                but before your server calls accept().
                                                                It means pending connections and does not limit the number of clients.
*/

typedef struct SERVER
{
    // socket configuration
    int                m_iDomain;
    int                m_iService;
    int                m_iProtocol;
    unsigned long      m_iInterface;
    int                m_iPort;
    int                m_iBacklog;

    // listening socket
    int                m_iListenFd;
    struct sockaddr_in m_si_address;

    // worker management
    int                m_iWorkerCount;
    pid_t              m_arrWorkers[MAX_WORKERS];
    bool               m_bRunning;
} SERVER;

/*===================================== Set up the Master ======================================*/
SERVER server_create
(
    int           iDomain,
    int           iService,
    int           iProtocol,
    unsigned long iInterface,
    int           iPort,
    int           iBacklog,
    int           iWorkerCount
);
int  server_setup_listener(SERVER* s_pServer); // create socket, bind, listen, set non-blocking 
int  server_spawn_workers (SERVER* s_pServer); // fork worker 
void server_master_loop   (SERVER* s_pServer);
void server_shutdown      (SERVER* s_pServer);

#endif

/*
 
server_create()         -> creates and fills most values of the SERVER struct variable
server_setup_listener() -> attaches a listening socket to the listening socket variable of the SERVER and sets it to non blocking and starts listening to it
server_spawn_workers()  -> spawns N number of workers that will respond to the requests on the listening socket
server_master_loop()    -> respawns any worker if it dies and replaces it pid in the SERVER struct's variable 
server_shutdown()       -> kills all the workers and shuts down the listening socket

*/

