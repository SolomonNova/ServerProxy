/*
    File name: stack.c
    Created at: 22-12-25
    Author: Solomon
*/

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "stack.h"

/////////////////////////////////////////
/////////////////////////////////////////
bool isEmpty(Stack* s)
{
    if (!s) return true;
    if (!s->top) return true;
    
    return false;
}
/////////////////////////////////////////
/////////////////////////////////////////
bool initialize(Stack* s, size_t capacity, size_t elementSize)
{
    if (!s || capacity == 0 || elementSize == 0) return false;

    s->top = 0;
    s->capacity = capacity;
    s->elementSize = elementSize;
    s->array = calloc(s->capacity, s->elementSize);
    if (s->array == NULL) return false;

    return true;
}

/////////////////////////////////////////
/////////////////////////////////////////
bool resizeStack(Stack* s, size_t sizeMultiplier)
{
    s->capacity = s->capacity * sizeMultiplier;
    void* temp = realloc(s->array, s->capacity * s->elementSize);
    if (temp == NULL) return false;
    s->array = temp;

    return true;
}

/////////////////////////////////////////
/////////////////////////////////////////
bool push(Stack* s, void* value)
{
    if (!s || !value) return false;
    
    if (s->top == s->capacity)
        if(!resizeStack(s, 2)) return false;
    
    char* base = (char*)s->array;
    memcpy(base + s->top * s->elementSize, value, s->elementSize);
    s->top++;

    return true;
}

/////////////////////////////////////////
/////////////////////////////////////////
void* pop(Stack* s)
{
    if (!s) return NULL;
    if (!s->top) return NULL;

    char* base = (char*)s->array;
    void* element = calloc(1, s->elementSize);
    if (!element) return NULL;
    memcpy(element, base + (s->top - 1) * s->elementSize, s->elementSize);
    s->top--;

    return element;
}

/////////////////////////////////////////
/////////////////////////////////////////
void destroy(Stack* s)
{
    free(s->array);
    s->array = NULL;
    s->capacity = s->top = s->elementSize = 0;
}

