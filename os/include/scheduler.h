#pragma once

#include <definitions.h>
#include <symbols.h>

#define yieldThread() __asm__ volatile ("int $0x67" : : : "memory")

typedef struct
{
    void* next;
    void* prev;
    uint64_t id;
    uint64_t waiting;
    Symbol* symbols;
    uint64_t symbolCount;
    uint8_t ttyId;
    bool user;
    uint8_t* kernelStack;
    uint64_t kernelStackTop;
    uint64_t userHeapCurrent;
    uint64_t userHeapEnd;
    uint64_t sp;
} Thread;

extern Thread* currentThread;

void initScheduler();

uint64_t createThread(void (*function)());
uint64_t createUserThread(void (*function)(), uint64_t userStack, uint64_t userHeap, uint64_t userHeapEnd);

void waitForThread(uint64_t id);

void destroyThread(uint64_t id);

void exitThread();
