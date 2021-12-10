#include "config.h"
#include "platform.h"

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#define XSTR(x) STR(x)
#define STR(x) #x

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#if DEBUG
#include <stdio.h>
#endif

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "control.h"
#include "command.h"
#include "shell.h"
#include "timer.h"


/* Step/dir */


void blink(void)
{
	static bool l = false;
	l = !l;
	if (l)
		PORTB |= 1 << 5;
	else
		PORTB &= ~(1 << 5);
}

// I/O shell vars


int main(void)
{
	shell_setup();

	sei();
	control_init();

	while (true)
	{
		const char *inbuf;
		size_t len;
		if (shell_input_ready(&inbuf, &len))
		{
			blink();
			handle_command(inbuf, len);
			shell_input_reset();
		}
	}
	return 0;
}
