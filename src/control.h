#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"

void control_init(void);
void control_set_current_position(const int32_t *pos);
bool control_add_target_delta(uint8_t tid, const int32_t *dpos, uint32_t delay);
void control_stop_moving(void);
bool control_current_position(uint8_t *tid, int32_t *pos);

// callbacks
void control_step_timer(void);
void control_step_reset_timer(void);
