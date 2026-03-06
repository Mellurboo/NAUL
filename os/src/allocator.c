#include <allocator.h>
#include <serial.h>
#include <syscalls.h>
#include <calls.h>
#include <cpu.h>
#include <scheduler.h>
#include <mem.h>

#define MEMORY_START 0x1000000

typedef struct
{
    bool present;
    uint64_t start;
    uint64_t end;
} Allocation;

Allocation* allocated = 0;
uint64_t allocations = 0;
bool allocating = false;

static void* allocateSyscall(uint64_t amount)
{
    if (currentThread && currentThread->user)
    {
        uint64_t value = currentThread->userHeapCurrent;
        if (value + amount > currentThread->userHeapEnd)
        {
            return 0;
        }
        currentThread->userHeapCurrent = value + amount;
        return (void*)value;
    }
    return allocate(amount);
}

static void* allocateAlignedSyscall(uint64_t amount, uint64_t alignment)
{
    if (currentThread && currentThread->user)
    {
        uint64_t value = currentThread->userHeapCurrent;
        if (value % alignment != 0)
        {
            value += alignment - (value % alignment);
        }
        if (value + amount > currentThread->userHeapEnd)
        {
            return 0;
        }
        currentThread->userHeapCurrent = value + amount;
        return (void*)value;
    }
    return allocateAligned(amount, alignment);
}

static void unallocateSyscall(void* pointer)
{
    if ((uint64_t)pointer >= PROCESS_ADDRESS)
    {
        return;
    }
    unallocate(pointer);
}

void initAllocator(uint64_t end)
{
    serialPrint("Setting up allocator");
    registerSyscall(ALLOCATE, allocateSyscall);
    registerSyscall(ALLOCATE_ALIGNED, allocateAlignedSyscall);
    registerSyscall(UNALLOCATE, unallocateSyscall);
    serialPrint("Storing allocation location");
    allocated = (Allocation*)(end - sizeof(Allocation));
    serialPrint("Set up allocator");
}

void markUnusable(uint64_t start, uint64_t end)
{
    Allocation* allocation = allocated;
    uint64_t count = 0;
    while (allocation->present && count != allocations)
    {
        count++;
        allocation--;
    }
    allocation->present = true;
    allocation->start = start;
    allocation->end = end;
    allocations++;
}

void* allocate(uint64_t amount)
{
    lock(&allocating);
    uint64_t value = MEMORY_START;
    Allocation* allocation = allocated;
    uint64_t count = 0;
    while (count != allocations)
    {
        if (allocation->present)
        {
            count++;
            if (value + amount <= allocation->start || value >= allocation->end)
            {
                allocation--;
            }
            else
            {
                value = allocation->end;
                allocation = allocated;
                count = 0;
            }
        }
        else
        {
            allocation--;
        }
    }
    allocation = allocated;
    count = 0;
    while (allocation->present && count != allocations)
    {
        count++;
        allocation--;
    }
    allocation->present = true;
    allocation->start = value;
    allocation->end = value + amount;
    allocations++;
    unlock(&allocating);
    return (void*)value;
}

void* allocateAligned(uint64_t amount, uint64_t alignment)
{
    lock(&allocating);
    uint64_t value = MEMORY_START + (alignment - (MEMORY_START % alignment));
    Allocation* allocation = allocated;
    uint64_t count = 0;
    while (count != allocations)
    {
        if (allocation->present)
        {
            count++;
            if (value + amount <= allocation->start || value >= allocation->end)
            {
                allocation--;
            }
            else
            {
                value = allocation->end + (alignment - (allocation->end % alignment));
                allocation = allocated;
                count = 0;
            }
        }
        else
        {
            allocation--;
        }
    }
    allocation = allocated;
    count = 0;
    while (allocation->present && count != allocations)
    {
        count++;
        allocation--;
    }
    allocation->present = true;
    allocation->start = value;
    allocation->end = value + amount;
    allocations++;
    unlock(&allocating);
    return (void*)value;
}

void unallocate(void* pointer)
{
    lock(&allocating);
    Allocation* test = allocated;
    while (!test->present || test->start != (uint64_t)pointer)
    {
        test--;
    }
    test->present = false;
    allocations--;
    unlock(&allocating);
}
