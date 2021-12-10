#include "config.h"
#include "steppers.h"

#include <avr/io.h>

// PORTD
#define SDPORT PORTD

#define XDIR 4
#define XSTEP 3

#define YDIR 6
#define YSTEP 5

#define ENABLE_X_DDR DDRD
#define ENABLE_X_PORT PORTD
#define ENABLE_X_PIN 2

#define ENABLE_Y_DDR DDRB
#define ENABLE_Y_PORT PORTB
#define ENABLE_Y_PIN 0

void steppers_configure(void)
{
	DDRD = 1 << YSTEP | 1 << YDIR | 1 << XSTEP | 1 << XDIR;
	ENABLE_X_DDR |= 1 << ENABLE_Y_PIN;
	ENABLE_Y_DDR |= 1 << ENABLE_Y_PIN;
}

void steppers_disable(void)
{
	ENABLE_X_PORT |= 1 << ENABLE_X_PIN;
	ENABLE_Y_PORT |= 1 << ENABLE_Y_PIN;
}

void steppers_enable(void)
{
	ENABLE_X_PORT &= ~(1 << ENABLE_X_PIN);
	ENABLE_Y_PORT &= ~(1 << ENABLE_Y_PIN);
}

void steppers_set_dir(int i, bool pos)
{
	if (i == 0)
	{
		if (pos)
		{
			SDPORT &= ~(1 << XDIR);
		}
		else
		{
			SDPORT |= (1 << XDIR);
		}
	}
	else
	{
		if (pos)
		{
			SDPORT &= ~(1 << YDIR);
		}
		else
		{
			SDPORT |= (1 << YDIR);
		}
	}
}

void steppers_step(int i)
{
	if (i == 0)
		SDPORT |= (1 << XSTEP);
	else
		SDPORT |= (1 << YSTEP);
}

void steppers_clear_step(int i)
{
	if (i == 0)
	{
		SDPORT &= ~(1 << XSTEP);
	}
	else
	{
		SDPORT &= ~(1 << YSTEP);
	}
}

