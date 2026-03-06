#include <scheduler.h>
#include <serial.h>
#include <syscalls.h>
#include <idt.h>
#include <allocator.h>
#include <hpet.h>
#include <calls.h>
#include <cpu.h>
#include <gdt.h>

#define SCHEDULER_INTERRUPT 32
#define STACK_SIZE 0x100000
#define APIC_BASE_ADDRESS 0xfee00000
#define LAPIC_EOI_REGISTER (APIC_BASE_ADDRESS + 0xB0)
#define LAPIC_DIVISOR_REGISTER (APIC_BASE_ADDRESS + 0x3E0)
#define LAPIC_RELOAD_COUNT (APIC_BASE_ADDRESS + 0x380)
#define LAPIC_COUNTER (APIC_BASE_ADDRESS + 0x390)
#define LAPIC_CONFIG_REGISTER (APIC_BASE_ADDRESS + 0x320)
#define LAPIC_PERIODIC_MODE 0x20000

typedef struct
{
    uint64_t ip;
    uint64_t cs;
    uint64_t flags;
    uint64_t sp;
    uint64_t ss;
} __attribute__((packed)) InterruptFrame;

Thread* threads = 0;
Thread* currentThread = 0;

void updateTssStack()
{
    setKernelStack(currentThread->kernelStackTop);
}

/*
    I moved this up from create thread
    it sorta breaks DRY rules but you can go ahead and fix it up if you'd like
    Myles
*/

static uint64_t allocateThreadId()
{
    uint64_t id = 0;
    Thread* current = threads;
    while (true)
    {
        if (current->id == id)
        {
            id++;
            current = threads;
        }
        else
        {
            current = current->next;
            if (current == threads)
            {
                break;
            }
        }
    }
    return id;
}

static uint64_t createThreadWithMode(void (*function)(), bool user, uint64_t userStack, uint64_t userHeap, uint64_t userHeapEnd)
{
    uint64_t flags = 0;
    __asm__ volatile ("pushfq; pop %0" : "=g"(flags));
    __asm__ volatile ("cli");
    Thread* thread = allocate(sizeof(Thread));
    thread->next = currentThread;
    thread->id = allocateThreadId();
    thread->waiting = 0;
    thread->symbols = currentThread->symbols;
    thread->symbolCount = currentThread->symbolCount;
    thread->ttyId = currentThread->ttyId;
    thread->user = user;
    thread->kernelStack = allocateAligned(STACK_SIZE, 16);
    thread->kernelStackTop = (uint64_t)thread->kernelStack + STACK_SIZE;
    thread->userHeapCurrent = 0;
    thread->userHeapEnd = 0;
    thread->sp = thread->kernelStackTop - sizeof(InterruptFrame) - sizeof(void (**)());

    InterruptFrame* frame = (InterruptFrame*)thread->sp;
    frame->ip = (uint64_t)function;
    frame->cs = user ? USER_CODE_SEGMENT : KERNEL_CODE_SEGMENT;
    frame->flags = flags;
    frame->ss = user ? USER_DATA_SEGMENT : KERNEL_DATA_SEGMENT;
    if (user)
    {
        userStack -= sizeof(uint64_t);
        *(uint64_t*)userStack = 0;
        frame->sp = userStack;
        thread->userHeapCurrent = userHeap;
        thread->userHeapEnd = userHeapEnd;
        *(uint64_t*)(thread->sp + sizeof(InterruptFrame)) = 0;
    }
    else
    {
        frame->sp = thread->sp + sizeof(InterruptFrame);
        *(void (**)())(thread->sp + sizeof(InterruptFrame)) = exitThread;
    }

    __asm__ volatile ("movq %%rsp, %0" : "=g"(currentThread->sp));
    currentThread = thread;
    __asm__ volatile ("movq %0, %%rsp" : : "g"(currentThread->sp));
    __asm__ volatile ("pushq %rax; pushq %rbx; pushq %rcx; pushq %rdx; pushq %rsi; pushq %rdi; pushq $0; pushq %rsp; pushq %r8; pushq %r9; pushq %r10; pushq %r11; pushq %r12; pushq %r13; pushq %r14; pushq %r15");
    pushAvxRegisters();
    pushCr3();
    __asm__ volatile ("movq %%rsp, %0" : "=g"(currentThread->sp));
    __asm__ volatile ("movq %1, %0" : "=m"(currentThread) : "r"(currentThread->next));
    __asm__ volatile ("movq %0, %%rsp" : : "g"(currentThread->sp));

    ((Thread*)threads->prev)->next = thread;
    thread->next = threads;
    thread->prev = threads->prev;
    threads->prev = thread;
    __asm__ volatile ("sti");
    return thread->id;
}

__attribute__((naked)) void nextThread()
{
    __asm__ volatile (
        "tryNext:\n\t"
        "movq currentThread(%%rip), %%rax\n\t"
        "movq %c[next](%%rax), %%rax\n\t"
        "movq %%rax, currentThread(%%rip)\n\t"
        "movq %c[waiting](%%rax), %%rdx\n\t"
        "testq %%rdx, %%rdx\n\t"
        "jnz tryNext\n\t"
        "call updateTssStack\n\t"
        "movq currentThread(%%rip), %%rax\n\t"
        "movq %c[sp](%%rax), %%rsp\n\t"
        "popq %%rax\n\t"
        "movq %%rax, %%cr3\n\t"
        "vmovdqu (%%rsp), %%ymm15\n\t"
        "vmovdqu 32(%%rsp), %%ymm14\n\t"
        "vmovdqu 64(%%rsp), %%ymm13\n\t"
        "vmovdqu 96(%%rsp), %%ymm12\n\t"
        "vmovdqu 128(%%rsp), %%ymm11\n\t"
        "vmovdqu 160(%%rsp), %%ymm10\n\t"
        "vmovdqu 192(%%rsp), %%ymm9\n\t"
        "vmovdqu 224(%%rsp), %%ymm8\n\t"
        "vmovdqu 256(%%rsp), %%ymm7\n\t"
        "vmovdqu 288(%%rsp), %%ymm6\n\t"
        "vmovdqu 320(%%rsp), %%ymm5\n\t"
        "vmovdqu 352(%%rsp), %%ymm4\n\t"
        "vmovdqu 384(%%rsp), %%ymm3\n\t"
        "vmovdqu 416(%%rsp), %%ymm2\n\t"
        "vmovdqu 448(%%rsp), %%ymm1\n\t"
        "vmovdqu 480(%%rsp), %%ymm0\n\t"
        "addq $512, %%rsp\n\t"
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        "popq %%r13\n\t"
        "popq %%r12\n\t"
        "popq %%r11\n\t"
        "popq %%r10\n\t"
        "popq %%r9\n\t"
        "popq %%r8\n\t"
        "popq %%rsp\n\t"
        "popq %%rbp\n\t"
        "popq %%rdi\n\t"
        "popq %%rsi\n\t"
        "popq %%rdx\n\t"
        "popq %%rcx\n\t"
        "popq %%rbx\n\t"
        "popq %%rax\n\t"
        "iretq"
        :
        : [next] "i"(__builtin_offsetof(Thread, next)),
          [waiting] "i"(__builtin_offsetof(Thread, waiting)),
          [sp] "i"(__builtin_offsetof(Thread, sp))
        : "%rax", "%rdx", "memory"
    );
}

__attribute__((naked)) void skipThread()
{
    pushRegisters();
    pushAvxRegisters();
    pushCr3();
    __asm__ volatile ("movq %%rsp, %0" : "=g"(currentThread->sp));
    __asm__ volatile ("jmp nextThread");
}

__attribute__((naked)) void updateScheduler()
{
    pushRegisters();
    pushAvxRegisters();
    pushCr3();
    __asm__ volatile ("movq %%rsp, %0" : "=g"(currentThread->sp));
    __asm__ volatile ("movl $0, %0" : "=m"(*(uint32_t*)LAPIC_EOI_REGISTER));
    __asm__ volatile ("jmp nextThread");
}

void initScheduler()
{
    serialPrint("Setting up scheduler");
    registerSyscall(CREATE_THREAD, createThread);
    registerSyscall(WAIT_FOR_THREAD, waitForThread);
    registerSyscall(DESTROY_THREAD, destroyThread);
    registerSyscall(EXIT_THREAD, exitThread);
    installIsrWithPrivilege(0x67, skipThread, 3);
    serialPrint("Creating main thread");
    threads = allocate(sizeof(Thread));
    threads->next = threads;
    threads->prev = threads;
    threads->id = 0;
    threads->waiting = 0;
    threads->symbols = 0;
    threads->symbolCount = 0;
    threads->ttyId = 0;
    threads->user = false;
    threads->kernelStack = 0;
    threads->userHeapCurrent = 0;
    threads->userHeapEnd = 0;
    __asm__ volatile ("movq %%rsp, %0" : "=g"(threads->sp));
    threads->kernelStackTop = threads->sp;
    currentThread = threads;
    setKernelStack(currentThread->kernelStackTop);
    serialPrint("Configuring timer");
    installIsr(SCHEDULER_INTERRUPT, updateScheduler);
    serialPrint("Setting timer divisor");
    *(uint32_t*)LAPIC_DIVISOR_REGISTER = 3;
    serialPrint("Calibrating timer");
    *(uint32_t*)LAPIC_RELOAD_COUNT = __UINT32_MAX__;
    uint64_t start = getFemtoseconds();
    while (getFemtoseconds() - start < FEMTOSECONDS_PER_MILLISECOND);
    uint32_t ticks = __UINT32_MAX__ - *(uint32_t*)LAPIC_COUNTER;
    serialPrint("Setting initial timer count");
    *(uint32_t*)LAPIC_RELOAD_COUNT = ticks;
    serialPrint("Setting interrupt vector");
    *(uint32_t*)LAPIC_CONFIG_REGISTER = SCHEDULER_INTERRUPT | LAPIC_PERIODIC_MODE;
    serialPrint("Set up scheduler");
}

// collapsed the callers here
uint64_t createThread(void (*function)())
{
    return createThreadWithMode(function, false, 0, 0, 0);
}

uint64_t createUserThread(void (*function)(), uint64_t userStack, uint64_t userHeap, uint64_t userHeapEnd)
{
    return createThreadWithMode(function, true, userStack, userHeap, userHeapEnd);
}

void waitForThread(uint64_t id)
{
    currentThread->waiting = id;
    yieldThread();
}

void destroyThread(uint64_t id)
{
    __asm__ volatile ("cli");
    Thread* current = threads;
    while (current->id != id)
    {
        current = current->next;
    }
    ((Thread*)current->prev)->next = current->next;
    ((Thread*)current->next)->prev = current->prev;
    if (current->kernelStack)
    {
        unallocate(current->kernelStack);
    }

    unallocate(current);
    current = threads;
    while (true)
    {
        if (current->waiting == id)
        {
            current->waiting = 0;
        }
        current = current->next;
        if (current == threads)
        {
            break;
        }
    }
    __asm__ volatile ("sti");
}

void exitThread()
{
    __asm__ volatile ("cli");
    Thread* current = threads;
    while (true)
    {
        if (current->waiting == currentThread->id)
        {
            current->waiting = 0;
        }
        current = current->next;
        if (current == threads)
        {
            break;
        }
    }
    Thread* prev = currentThread->prev;
    prev->next = currentThread->next;
    ((Thread*)currentThread->next)->prev = prev;
    if (currentThread->kernelStack)
    {
        unallocate(currentThread->kernelStack);
    }

    unallocate(currentThread);
    currentThread = prev;
    __asm__ volatile ("jmp nextThread");
}
