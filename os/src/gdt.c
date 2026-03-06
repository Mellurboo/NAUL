#include <gdt.h>
#include <definitions.h>
#include <serial.h>

/* these are a bit hard to maintain i added some of mine below you can add yours back if you
    prefer them tho
        Myles
#define GDT_LONG_MODE 0x20000000000000
#define GDT_PRESENT 0x800000000000
#define GDT_CODE_DATA 0x100000000000
#define GDT_EXECUTABLE 0x80000000000
#define GDT_READ_WRITE 0x20000000000
#define GDT_USER_MODE 0x600000000000
#define GDT_TSS_ACCESS 0x89
*/

#define GDT_PRESENT       (1ULL << 47)
#define GDT_USER_MODE     (3ULL << 45)
#define GDT_CODE_DATA     (1ULL << 44)
#define GDT_EXECUTABLE    (1ULL << 43)
#define GDT_READ_WRITE    (1ULL << 41)
#define GDT_LONG_MODE     (1ULL << 53)
#define GDT_TSS_ACCESS    0x89

typedef struct
{
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t ioMapBase;
} __attribute__((packed)) Tss;

uint64_t gdt[7] =
{
    0x0,
    GDT_PRESENT | GDT_LONG_MODE | GDT_CODE_DATA | GDT_EXECUTABLE | GDT_READ_WRITE,
    GDT_PRESENT | GDT_CODE_DATA | GDT_READ_WRITE,
    GDT_PRESENT | GDT_LONG_MODE | GDT_CODE_DATA | GDT_EXECUTABLE | GDT_READ_WRITE | GDT_USER_MODE,
    GDT_PRESENT | GDT_CODE_DATA | GDT_READ_WRITE | GDT_USER_MODE,
    0x0,
    0x0
};

Tss tss = { 0 };
const struct {
    uint16_t length;
    uint64_t base;
} __attribute__((packed)) gdtr = { sizeof(gdt) - 1, (uint64_t)gdt };

__attribute__((naked)) void loadGdt()
{
    __asm__ volatile ("lgdt %0; pushq $0x08; leaq trampoline(%%rip), %%rax; pushq %%rax; retfq" : : "m"(gdtr) : "%rax");
    __asm__ volatile ("trampoline:");
    __asm__ volatile ("movw $0x10, %ax; movw %ax, %ds; movw %ax, %es; movw %ax, %fs; movw %ax, %gs; movw %ax, %ss; retq");
}

// https://wiki.osdev.org/Task_State_Segment
static void initTss()
{
    uint64_t address = (uint64_t)&tss;
    uint64_t limit = sizeof(Tss) - 1;
    gdt[5] = (limit & 0xFFFF)
           | ((address & 0xFFFFFF) << 16)
           | ((uint64_t)GDT_TSS_ACCESS << 40)
           | (((limit >> 16) & 0xF) << 48)
           | (((address >> 24) & 0xFF) << 56);
    gdt[6] = address >> 32;
    tss.ioMapBase = sizeof(Tss);
}

void initGdt()
{
    serialPrint("Setting up GDT");
    initTss();
    loadGdt();
    __asm__ volatile ("movw $0x28, %%ax; ltr %%ax" : : : "%ax");
    serialPrint("Set up GDT");
}

void setKernelStack(uint64_t stack)
{
    tss.rsp0 = stack;
}
