#include <avr/io.h>
#include <avr/interrupt.h>

#include "platform.h"
#include "control.h"

void timer_start(uint32_t period_us)
{
	uint16_t period = period_us / GOTO_TICK_US;
	if (period < 8)
		period = 8;

	TIMSK1 = 0;
	TCCR1A = (0 << WGM11) | (0 << WGM10);
	TCCR1C = 0;
	OCR1A = period;
	OCR1B = 2;
	TIMSK1 |= (1 << OCIE1A) | (1 << OCIE1B);
	TCCR1B = (GOTO_PSC2 << CS12) | (GOTO_PSC1 << CS11) | (GOTO_PSC0 << CS10) | (1 << WGM12) | (0 << WGM13); // pre-scaler 64, CTC
	TCNT1 = 0;
}

void timer_stop(void)
{
	TCCR1A = 0;
	TCCR1B = 0;
}

// Timer for stepping
ISR(TIMER1_COMPA_vect)
{
	control_step_timer();
}

// Timer for clearing steps
ISR(TIMER1_COMPB_vect)
{
	control_step_reset_timer();
}
