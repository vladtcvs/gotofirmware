#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#include "platform.h"

void shell_print_ok(void);
void shell_print_err(int err);
void shell_print_pos(uint8_t tid, int32_t x, int32_t y);
void shell_setup(void);
bool shell_input_ready(const char **inbuf, size_t *len);
void shell_input_reset(void);

#if DEBUG
void shell_print_debug(const char *str);
#endif
