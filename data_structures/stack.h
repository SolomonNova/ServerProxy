/*
    File name: Stack.h
    Created at: 22-12-25
    Author: Solomon
*/

#ifndef STACK_H
#define STACK_H

#include <string.h>
#include <stdbool.h>

typedef struct Stack
{
    size_t top; // top of stack where next element should be written
    size_t capacity;
    size_t elementSize;
    void* array;
} Stack;

bool isEmpty(Stack* s);
bool initialize(Stack* s, size_t capacity, size_t elementSize);
bool resizeStack(Stack* s, size_t sizeMultiplier);
bool push(Stack* s, void* value);
void* pop(Stack* s);
void destroy(Stack* s);

#endif