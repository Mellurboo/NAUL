#pragma once

#include <definitions.h>

void initIdt();

void installIsr(uint8_t interrupt, void (*handler)());
void installIsrWithPrivilege(uint8_t interrupt, void (*handler)(), uint8_t privilege);

void installIrq(uint8_t interrupt, void (*handler)());
