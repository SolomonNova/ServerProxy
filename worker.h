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

void worker_run(struct SERVER* s_pServer);

#endif 
