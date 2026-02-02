/*
    File name    : worker.h
    creation date: 23-01-26
    Author       : Solomon
*/

#pragma once
#ifndef WORKER_H
#define WORKER_H


// forward deceleration 
typedef struct SERVER SERVER;

/*=================================================================Worker API======================================================*/

void worker_run            (struct SERVER* s_pServer);
int  worker_init           (int* pEpollFd);          
void worker_event_loop     (struct SERVER* s_pServer, int iEpollFd);
void worker_accept_clients (struct SERVER* s_pServer, int iEpollFd);
void worker_handle_client  (int iClientFd);
int  worker_set_nonblocking(int iFd);
void worker_cleanup        (struct SERVER* s_pServer, int iEpollFd);



#endif 
