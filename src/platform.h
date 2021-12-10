#pragma once

#include <stdint.h>
#include "config.h"

#ifdef ARDUINO
#define MEGA328P 1
#else
#define TINY4313 1
#endif

/* For arduino prototype */
#if MEGA328P

#define UDR UDR0
#define UCSRA UCSR0A
#define UCSRB UCSR0B
#define UCSRC UCSR0C

#define RXCIE RXCIE0
#define TXCIE TXCIE0
#define RXEN RXEN0
#define TXEN TXEN0

#define UBRR UBRR0
#define UBRRL UBRR0L
#define UBRRH UBRR0H

#define UDRE UDRE0

#elif TINY4313

#define TIMSK1 TIMSK
#define TIMSK0 TIMSK

#else
#error "Unsupported platform!"
#endif

// Number of axis
#define NDIM 2

static const uint32_t DIMS[NDIM] = {X_STEPS, Y_STEPS};
