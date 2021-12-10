#include <avr/io.h>
#include <avr/interrupt.h>

#include "control.h"
#include "steppers.h"
#include "timer.h"

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#include "platform.h"

#if DEBUG
#include "shell.h"
#include <stdio.h>
#endif

#define QUEUE_LEN 2

#define abs(x) ((x) > 0 ? (x) : (-(x)))
#define sign(x) ((x) >= 0 ? 1 : (-1))

struct delta_s
{
	uint8_t tid;
	int32_t dpos[2];
	uint32_t delay;
};

struct position_s
{
	int32_t pos[NDIM];
};

// Bresenham algorythm
static uint32_t delta[NDIM], delta_error[NDIM];
static uint32_t error[NDIM];
static uint32_t a;
static int main_axis;
static int8_t dirs[NDIM];

static struct position_s position;
static struct delta_s targets[QUEUE_LEN];
static size_t len = 0;
static volatile bool abort_move = false;

static void run_target(void);

static void control_target_completed(void)
{
	timer_stop();
	int i;
	for (i = 0; i < len - 1; i++)
	{
		int j;
		targets[i].tid = targets[i + 1].tid;
		for (j = 0; j < NDIM; j++)
			targets[i].dpos[j] = targets[i + 1].dpos[j];
		targets[i].delay = targets[i + 1].delay;
	}
	if (len > 0)
		len--;

	if (len > 0)
		run_target();
}

void control_set_current_position(const int32_t *pos)
{
	int i;
	for (i = 0; i < NDIM; i++)
		position.pos[i] = pos[i];
}

bool control_add_target_delta(uint8_t tid, const int32_t *dpos, uint32_t delay)
{
	int i;
	abort_move = false;
	if (len < QUEUE_LEN)
	{
		for (i = 0; i < NDIM; i++)
			targets[len].dpos[i] = dpos[i];

		targets[len].tid = tid;
		targets[len].delay = delay;
		len++;
		if (len == 1)
			run_target();
		return true;
	}
	return false;
}

void control_stop_moving(void)
{
	abort_move = true;
}

bool control_current_position(uint8_t *tid, int32_t *pos)
{
	int i;
	for (i = 0; i < NDIM; i++)
		pos[i] = position.pos[i];
	if (len > 0)
	{
		*tid = targets[0].tid;
		return true;
	}
	*tid = 0;
	return false;
}

static int32_t cyclic_add(int32_t pos, int dir, int32_t maxpos)
{
	if (pos == 0 && dir == -1)
		return maxpos - 1;
	if (pos == maxpos - 1 && dir == 1)
		return 0;
	return pos + dir;
}

void control_step_timer(void)
{
	if (abort_move)
	{
		abort_move = false;
		len = 0;
		return;
	}

	if (a < delta[main_axis])
	{
		steppers_step(main_axis);
		position.pos[main_axis] = cyclic_add(position.pos[main_axis], dirs[main_axis], DIMS[main_axis]);

		a++;
		int j;
		for (j = 0; j < NDIM; j++)
		{
			if (j == main_axis)
				continue;

			error[j] += delta_error[j];
			if (error[j] >= (delta[main_axis]+1))
			{
				error[j] -= (delta[main_axis]+1);
				steppers_step(j);
				position.pos[j] = cyclic_add(position.pos[j], dirs[j], DIMS[j]);
			}
		}
	}

	if (a >= delta[main_axis])
	{
		control_target_completed();
	}
}

void control_step_reset_timer(void)
{
	steppers_clear_step(0);
	steppers_clear_step(1);
}

static void run_target(void)
{
	steppers_enable();
	int i;
	bool nomove = true;
	uint32_t maxd = 0;

	for (i = 0; i < NDIM; i++)
	{
		steppers_set_dir(i, targets[0].dpos[i] >= 0);
		delta[i] = abs(targets[0].dpos[i]);
		dirs[i] = sign(targets[0].dpos[i]);
		if (delta[i] != 0)
			nomove = false;
		if (delta[i] > maxd)
		{
			maxd = delta[i];
			main_axis = i;
		}
	}

	if (nomove)
	{
		control_target_completed();
		return;
	}

	a = 0;

	for (i = 0; i < NDIM; i++)
	{
		if (i == main_axis)
			continue;
		delta_error[i] = delta[i] + 1;
		error[i] = 0;
	}
/*
	#if DEBUG
	{
		char buf[150];
		snprintf(buf, 150, "debug, ax: %i, da : %ld, dbx: %ld dby: %ld", main_axis, (long)delta[main_axis], (long)delta[0], (long)delta[1]);
		shell_print_debug(buf);
	}
	#endif
*/
	timer_start(targets[0].delay);
}

void control_init(void)
{
	int i;
	for (i = 0; i < NDIM; i++)
		position.pos[i] = DIMS[i]/2;
}
