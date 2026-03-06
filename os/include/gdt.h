#pragma once

#include <definitions.h>

#define KERNEL_CODE_SEGMENT 0x08
#define KERNEL_DATA_SEGMENT 0x10
#define USER_CODE_SEGMENT 0x1B
#define USER_DATA_SEGMENT 0x23

void initGdt();

void setKernelStack(uint64_t stack);
