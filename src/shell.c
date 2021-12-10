#include <avr/io.h>
#include <avr/interrupt.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "shell.h"
#include "config.h"
#include "platform.h"

#if DEBUG
#include <stdio.h>
#endif


#define UBRR_VALUE F_CPU / 16 / (BAUD)-1

/* I/O options */
#if DEBUG

#define INBUFLEN 200
#define OUTBUFLEN 200

#else

#define INBUFLEN 64
#define OUTBUFLEN 64

#endif

static char outbuf[OUTBUFLEN];
static uint8_t outpos;

static volatile bool outbuf_rdy;
static volatile bool transmit = false;

static char inbuf[INBUFLEN];
static uint8_t inpos;
static volatile bool inbuf_rdy;

static void itoa32_octal(int32_t val, char *s)
{
	int len = 0, len2;
	while (*s != 0)
		s++;

	if (val == 0)
	{
		s[0] = '0';
		s[1] = 0;
		return;
	}
	
	if (val < 0)
	{
		s[0] = '-';
		s++;
		val = -val;
	}

	int32_t v = val;
	while (v > 0)
	{
		len++;
		v /= 8;
	}

	len2 = len;

	while (len > 0)
	{
		s[len - 1] = val % 8 + '0';
		val /= 8;
		len--;
	}

	s += len2;

	*(s++) = 0;
}

static void start_transmit(void)
{
	while (!(UCSRA & (1 << UDRE)))
		;

	outpos = 1;
	UCSRB &= ~(1 << RXCIE);
	transmit = true;
	outbuf_rdy = false;
	UDR = outbuf[0];
}

void shell_print_ok(void)
{
	outbuf[0] = 'o';
	outbuf[1] = 'k';
	outbuf[2] = '\r';
	outbuf[3] = '\n';
	outbuf[4] = 0;
	start_transmit();	
}

void shell_print_err(int err)
{
	int i;
	outbuf[0] = 'e';
	outbuf[1] = 'r';
	outbuf[2] = 'r';
	outbuf[3] = 0;

	itoa32_octal(err, outbuf);
	for (; i < OUTBUFLEN; i++)
		if (outbuf[i] == 0)
			break;
	outbuf[i++] = '\r';
	outbuf[i++] = '\n';
	outbuf[i++] = 0;

	start_transmit();
}

void shell_print_pos(uint8_t tid, int32_t x, int32_t y)
{
	uint8_t i = 0;
	
	memset(outbuf, 0, OUTBUFLEN);

	itoa32_octal(tid, outbuf + i);
	for (; i < OUTBUFLEN; i++)
		if (outbuf[i] == 0)
			break;
	outbuf[i] = ' ';
	i++;

	itoa32_octal(x, outbuf + i);
	for (; i < OUTBUFLEN; i++)
		if (outbuf[i] == 0)
			break;
	outbuf[i] = ' ';
	i++;

	itoa32_octal(y, outbuf + i);
	for (; i < OUTBUFLEN; i++)
		if (outbuf[i] == 0)
			break;
	outbuf[i] = '\r';
	i++;
	outbuf[i] = '\n';
	i++;
	outbuf[i] = 0;
	i++;

	outbuf[OUTBUFLEN - 1] = 0;

	start_transmit();
}

#if DEBUG
void shell_print_debug(const char *str)
{
	int len = strlen(str);
	outbuf[0] = ':';
	memcpy(outbuf + 1, str, len);
	len++;
	outbuf[len++] = '\r';
	outbuf[len++] = '\n';
	outbuf[len++] = 0;

	start_transmit();
	while (transmit)
		;
}
#endif

static void send_nl(void)
{
  if (!transmit)
  {
    UDR = '\n';
  }
}

ISR(USART_TX_vect)
{
  if (!transmit)
    return;
    
  if (outpos < OUTBUFLEN && outbuf[outpos] != 0)
  {
    UDR = outbuf[outpos++];
  }
  else
  {
    transmit = false;
    outbuf_rdy = true;
    UCSRB |= 1 << RXCIE;
  }
}

ISR(USART_RX_vect)
{
	char c = UDR;
	switch (c)
	{
	case '\r':
	case '\n':
	{
		inbuf_rdy = true;
		return;
	}
	default:
		if (inpos == INBUFLEN)
		{
			// error
			inpos = 0;
			return;
		}
		inbuf[inpos++] = c;
		return;
	}
}

void shell_setup(void)
{
	inpos = 0;
	UCSRB = 1 << RXCIE | 1 << TXCIE | 1 << RXEN | 1 << TXEN;
	UBRRH = UBRR_VALUE >> 8;
	UBRRL = UBRR_VALUE & 0xFF;
	DDRB = 1 << 5;
}

bool shell_input_ready(const char **inbuf_v, size_t *len_v)
{
	*inbuf_v = inbuf;
	*len_v = inpos;
	return inbuf_rdy;
}

void shell_input_reset(void)
{
	inpos = 0;
	inbuf_rdy = false;
}
