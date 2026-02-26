/*
    File name    : worker.h
    creation date: 23-01-26
    Author       : Solomon
*/

#ifndef WORKER_H
#define WORKER_H


// forward deceleration 
typedef struct SERVER SERVER;
typedef struct REQUEST_INFO REQUEST_INFO;


void worker_run(SERVER* s_pServer);
void handle_application_request(int iClientFd, const REQUEST_INFO *ri);

#endif 
