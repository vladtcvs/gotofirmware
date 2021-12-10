#pragma once

#include <stdbool.h>

void steppers_disable(void);
void steppers_enable(void);

void steppers_set_dir(int i, bool pos);
void steppers_step(int i);
void steppers_clear_step(int i);
