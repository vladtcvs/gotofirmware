#include "config.h"

#ifdef ARDUINO

#define MEGA328P 1

#else

#define TINY4313 1

#endif

#ifndef DEBUG
#define DEBUG 1
#endif

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

/* I/O options */
#if DEBUG

#define INBUFLEN 200
#define OUTBUFLEN 200

#else

#define INBUFLEN 20
#define OUTBUFLEN 20

#endif

#define UBRR_VALUE F_CPU / 16 / (BAUD) - 1

/* Step/dir */

// PORTD
#define SDPORT                      PORTD
#define HADIR                       4
#define HASTEP                      3
#define DECDIR                      6
#define DECSTEP                     5

#define ENABLE_HA_DDR              DDRD
#define ENABLE_HA_PORT             PORTD
#define ENABLE_HA_PIN              2

#define ENABLE_DEC_DDR               DDRB
#define ENABLE_DEC_PORT              PORTB
#define ENABLE_DEC_PIN               0

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


/*
 * Commands
 * 
 * G <H.A.> <Dec.>
 * Returns: none
 *
 * H <H.A. speed>
 * Returns: none
 *
 * P
 * Returns: <H.A.> <Dec.> <G|H>
 * 
 * S <H.A.> <Dec.>
 * Returns: none
 * 
 */

void blink(void)
{
  static bool l = false;
  l = !l;
  if (l)
    PORTB |= 1<<5;
  else
    PORTB &= ~(1<<5);
}

// I/O shell vars

char inbuf[INBUFLEN];
uint8_t inpos;
volatile bool inbuf_rdy;

char outbuf[OUTBUFLEN];
uint8_t outpos;

volatile bool outbuf_rdy;
volatile bool transmit = false;

// HA mode vars

volatile uint32_t delay_ha;          // Delay between steps for H.A.
volatile uint32_t counter_ha;

volatile uint32_t delay_dec;         // Delay between steps for Dec.
volatile uint32_t counter_dec;

int32_t  ha_speed;          // H.A. speed in H mode
int32_t  dec_speed;         // Dec. speed in H mode

// GOTO mode vars

uint32_t delta_x, x;
uint32_t delta_y;
uint32_t error, delta_error;

bool ha_is_x;

// COMMON vars

volatile bool isgoto;     // system in goto mode

uint32_t ha;              // in steps
int32_t  dec;              // in steps

bool ha_pos_dir;          // H.A. step direction
bool dec_pos_dir;         // Dec. step direction
bool dec_orientation;     // Dec. positive direction

static int32_t atoi32(const char *s)
{
  int sign = 1;
  int32_t val = 0;
  if (*s == '-')
  {
    sign = -1;
    s++;
  }
  else if (*s == '+')
  {
    s++;
  }

  while (*s >= '0' && *s <= '9')
  {
    val = val * 10 + (*s - '0');
    s++;
  }
  val *= sign;
  return val;
}

static void itoa32(int32_t val, char *s)
{
  if (val == 0)
  {
    *(s++) = '+';
    *(s++) = '0';
  }
  else if (val < 0)
  {
    *(s++) = '-';
    val = -val;
  }
  else
  {
    *(s++) = '+';
  }
  
  int len = 0, len2;
  int32_t v = val;
  while (v > 0)
  {
    len++;
    v /= 10;
  }

  len2 = len;

  while (len > 0)
  {
    s[len-1] = val % 10 + '0';
    val /= 10;
    len--;
  }

  s += len2;

  *(s++) = 0;
}

static void print_ok(void)
{
  outbuf[0] = 'o';
  outbuf[1] = 'k';
  outbuf[2] = '\r';
  outbuf[3] = '\n';
  outbuf[4] = 0;

  while (!(UCSRA & (1 << UDRE)))
    ;

  outpos = 1;
  UCSRB &= ~(1 << RXCIE);
  transmit = true;
  outbuf_rdy = false;
  UDR = outbuf[0];
}

#if DEBUG
static void print_debug(const char *str)
{
  int len = strlen(str);
  outbuf[0] = ':';
  memcpy(outbuf+1, str, len);
  len++;
  outbuf[len++] = '\r';
  outbuf[len++] = '\n';
  outbuf[len++] = 0;
  outpos = 1;

  while (!(UCSRA & (1 << UDRE)))
    ;

  UCSRB &= ~(1 << RXCIE);
  transmit = true;
  outbuf_rdy = false;
  UDR = outbuf[0];
  while (!outbuf_rdy)
    ;
}
#endif

static void print_pos(uint32_t ha, int32_t dec)
{
  uint8_t i = 0;
  uint32_t ha_s = ha  * HA_2_STEPS_B / HA_2_STEPS_A;    // HA_TOTAL_SECONDS / HA_STEPS;
  int32_t dec_s = dec * DEC_2_STEPS_B / DEC_2_STEPS_A;  // DEC_TOTAL_SECONDS / DEC_STEPS;

#if DEBUG
  {
    char buf[100];
    snprintf(buf, 100, "steps %ld %ld", (long)ha, (long)dec);
    print_debug(buf);
  }
#endif

  memset(outbuf, 0, OUTBUFLEN);

  itoa32(ha_s, outbuf);
  for (; i < OUTBUFLEN; i++)
    if (outbuf[i] == 0)
      break;
  outbuf[i] = ' ';
  i++;
  itoa32(dec_s, outbuf + i);
  
  for (; i < OUTBUFLEN; i++)
    if (outbuf[i] == 0)
      break;
  outbuf[i] = ' ';
  i++;
  if (isgoto)
    outbuf[i] = 'G';
  else
    outbuf[i] = 'H';
  outbuf[i+1] = '\r';
  outbuf[i+2] = '\n';

  outbuf[OUTBUFLEN-1] = 0;
  outpos = 1;

  while (!(UCSRA & (1 << UDRE)))
    ;

  transmit = true;
  UCSRB &= ~(1 << RXCIE);
  outbuf_rdy = false;
  UDR = outbuf[0];
}

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

static void dir_ha(bool pos)
{
  ha_pos_dir = pos;
  if (pos)
  {
    SDPORT &= ~(1<<HADIR);
  }
  else
  {
    SDPORT |= (1<<HADIR);
  }
}

static void dir_dec(bool pos)
{
  dec_pos_dir = pos;
  if (pos ^ dec_orientation)
  {
    SDPORT &= ~(1<<DECDIR);
  }
  else
  {
    SDPORT |= (1<<DECDIR);
  }
}

static void step_ha(void)
{
  if (ha_pos_dir)
  {
    if (ha == HA_STEPS-1)
      ha = 0;
    else
      ha++;
  }
  else
  {
    if (ha == 0)
      ha = HA_STEPS-1;
    else
      ha--;
  }

  SDPORT |= (1<<HASTEP);
}

static void step_dec(void)
{
  if (dec_pos_dir)
  {
    dec++;
  }
  else
  {
    dec--;
  }

  SDPORT |= (1<<DECSTEP);
}

static void clear_step_ha(void)
{
  SDPORT &= ~(1 << HASTEP);
}

static void clear_step_dec(void)
{
  SDPORT &= ~(1 << DECSTEP);
}

static void clear_steps(void)
{
  SDPORT &= ~(1 << HASTEP | 1 << DECSTEP);
}

void set_secs_per_hour(int32_t ha_secs_per_hour, int32_t dec_secs_per_hour);

void make_step_goto(void)
{
  bool make_y_step = false;
  error += delta_error;
    
  if (error >= delta_x + 1)
  {
    error -= (delta_x + 1);
    make_y_step = true;
  }
    
  if (ha_is_x)
  {
    // Make H.A. step
    step_ha();

    // Make Dec. step, if need
    if (make_y_step)
      step_dec();
  }
  else
  {
    // Make Dec. step
    step_dec();

    // Make H.A. step, if need
    if (make_y_step)
      step_ha();
  }

  x++;
  if (x == delta_x)
  {
    isgoto = false;
    set_secs_per_hour(ha_speed, dec_speed);
  }
}

ISR(TIMER1_COMPA_vect)
{
  if (isgoto)
  { 
    make_step_goto();
  }
  else
  {
    step_ha();
  }
}

ISR(TIMER1_COMPB_vect)
{
  if (isgoto)
    clear_steps();
  else
    clear_step_ha();
}

ISR(TIMER0_COMPA_vect)
{
  counter_dec++;
  if (counter_dec == delay_dec)
  {
    counter_dec = 0;
    step_dec();
  }
}

ISR(TIMER0_COMPB_vect)
{
  clear_step_dec();
}

// secs_per_hour in fixed point format
void set_secs_per_hour(int32_t ha_secs_per_hour, int32_t dec_secs_per_hour)
{
  cli();

  TIMSK0 = 0;
  TIMSK1 = 0;
  
  ha_speed = ha_secs_per_hour;
  dec_speed = dec_secs_per_hour;
  
  isgoto = false;
  
  dir_ha(ha_secs_per_hour >= 0);
  dir_dec(dec_secs_per_hour >= 0);
  
  ha_secs_per_hour = abs(ha_secs_per_hour);
  dec_secs_per_hour = abs(dec_secs_per_hour);

  // H.A.
  if (ha_secs_per_hour != 0)
  {
    ENABLE_HA_PORT  &= ~(1<<ENABLE_HA_PIN);

    delay_ha = ROTATION_BASE_DELAY_HA / ha_secs_per_hour;
    
    if (delay_ha >= 65536)
      delay_ha = 65535;
    else if (delay_ha > 0 && delay_ha < 32)
      delay_ha = 32;
    
    TCCR1A = (0 << WGM11) | (0 << WGM10);
    TCCR1C = 0;
    OCR1A = delay_ha;
    OCR1B = 2;
    TIMSK1 |= (1 << OCIE1A) | (1 << OCIE1B);
    TCCR1B = (ROTATION_PSC2_HA << CS12) | (ROTATION_PSC1_HA << CS11) | (ROTATION_PSC0_HA << CS10) | (1 << WGM12) | (0 << WGM13); // pre-scaler 1024, CTC
    TCNT1 = 0;
  }
  else
  {
    ENABLE_HA_PORT |=  (1<<ENABLE_HA_PIN);
    delay_ha = 0;
    TCCR1B = 0;
  }

  // DEC
  if (dec_secs_per_hour != 0)
  {
    ENABLE_DEC_PORT &=  ~(1<<ENABLE_DEC_PIN);

    delay_dec = ROTATION_BASE_DELAY_DEC / ROTATION_DIVISOR_DEC / dec_secs_per_hour;

    if (delay_dec < 4)
      delay_dec = 4;
    
    TCCR0A = (1 << WGM01) | (0 << WGM00);
    OCR0A = ROTATION_DIVISOR_DEC;
    OCR0B = 2;
    TIMSK0 |= (1 << OCIE0A) | (1 << OCIE0B);
    TCCR0B = (ROTATION_PSC2_DEC << CS02) | (ROTATION_PSC1_DEC << CS01) | (ROTATION_PSC0_DEC << CS00) | (0 << WGM02); // pre-scaler 1024, CTC
    TCNT0 = 0;
    counter_dec = 0;
  }
  else
  {
    ENABLE_DEC_PORT |=  (1<<ENABLE_DEC_PIN);
    delay_dec = 0;
    TCCR0B = 0;
  }
  sei();
}

void set_target_position(uint32_t tha, int32_t tdec)
{
  cli();

  TIMSK0 = 0;
  TIMSK1 = 0;

  delay_ha = 0;
  delay_dec = 0;
  counter_ha = 0;
  counter_dec = 0;
 
  int32_t dha = (int32_t)tha - ha;
  int32_t ddec = tdec - dec;

  if (dha > (int32_t)HA_STEPS / 2)
    dha -= (int32_t)HA_STEPS;
  else if (-dha > (int32_t)HA_STEPS / 2)
    dha += (int32_t)HA_STEPS;

  ENABLE_HA_PORT  &=  ~(1<<ENABLE_HA_PIN);
  ENABLE_DEC_PORT &=  ~(1<<ENABLE_DEC_PIN);

  dir_ha(dha >= 0);
  dha = (dha >= 0 ? dha : -dha);

  dir_dec(ddec >= 0);
  ddec = (ddec >= 0 ? ddec : -ddec);

  uint32_t counter;
  
  if (dha > ddec)
  {
    // use H.A. as main axis
    delta_x = dha;
    delta_y = ddec;
    ha_is_x = true;
    counter = GOTO_TIMER_COUNTER_HA;
  }
  else
  {
    // use Dec. as main axis
    delta_x = ddec;
    delta_y = dha;
    ha_is_x = false;
    counter = GOTO_TIMER_COUNTER_DEC;
  }

  if (delta_x == 0)
  {
    sei();
    return;
  }
  x = 0;
  error = 0;
  delta_error = delta_y + 1;

  isgoto = true;
  TCCR0B = 0;

  TCCR1A = (0 << WGM11) | (0 << WGM10);
  TCCR1C = 0;
  OCR1A = counter;
  OCR1B = 2;
  TIMSK1 |= (1 << OCIE1A) | (1 << OCIE1B);
  TCCR1B = (GOTO_PSC2 << CS12) | (GOTO_PSC1 << CS11) | (GOTO_PSC0 << CS10) | (1 << WGM12) | (0 << WGM13); // pre-scaler 64, CTC
  TCNT1 = 0;
  sei();
  
#if DEBUG
  {
    char buf[100];
    snprintf(buf, 100, "delta_x = %ld, delta_y = %ld", (unsigned long)delta_x, (unsigned long)delta_y);
    print_debug(buf);
  }
#endif
}

void read_int(int32_t *v)
{
  uint8_t i = 0;
  while (i < INBUFLEN && inbuf[i] != ' ')
    i++;
  i++;
  if (i >= INBUFLEN)
  {
    *v = 0;
    return;
  }
  *v = atoi32(inbuf + i);
}

void read_bool(bool *v)
{
  uint8_t i = 0;
  while (i < INBUFLEN && inbuf[i] != ' ')
    i++;
  i++;
  if (i >= INBUFLEN)
  {
    *v = false;
    return;
  }
  *v = (inbuf[i] == 'T');
}

bool read_2_int(int32_t *ha, int32_t *dec)
{
  int i = 0;
  while (i < INBUFLEN && inbuf[i] != ' ')
    i++;
  i++;
  if (i >= INBUFLEN)
    return false;
  
  *ha = atoi32(inbuf + i);

  while (i < INBUFLEN && inbuf[i] != ' ')
    i++;
  i++;
  if (i >= INBUFLEN)
    return false;

  *dec = atoi32(inbuf + i);

  return true;
}

void handle_command(void)
{
  if (inpos == 0)
    return;
  switch (inbuf[0])
  {
    case 'D': // Disable steppers
    {
      set_secs_per_hour(0, 0);
      ENABLE_HA_PORT |= 1<<ENABLE_HA_PIN;
      ENABLE_DEC_PORT |= 1<<ENABLE_DEC_PIN;
      print_ok();
      return;
    }
    case 'S': // Set current position, H.A. Dec
    {
      uint32_t ha_s;
      int32_t dec_s;
      read_2_int(&ha_s, &dec_s);
      
      ha  = (ha_s * HA_2_STEPS_A / HA_2_STEPS_B) % HA_STEPS;  // (((uint64_t)ha_s * HA_STEPS) / HA_TOTAL_SECONDS) % HA_STEPS;
      dec =  dec_s * DEC_2_STEPS_A / DEC_2_STEPS_B;           // ((int64_t)dec_s * DEC_STEPS) / DEC_TOTAL_SECONDS;
      print_ok();
      return;
    }
    case 'H': // H.A. and Dec. axis movement with speed. Speed in fixed point format
    {
      int32_t ha_secs_per_hour, dec_secs_per_hour;
      read_2_int(&ha_secs_per_hour, &dec_secs_per_hour);
      set_secs_per_hour(ha_secs_per_hour, dec_secs_per_hour);
      print_ok();
      return;
    }
    case 'G': // Goto "H.A. Dec"
    {
      uint32_t ha_s, tha;
      int32_t dec_s, tdec;
      read_2_int(&ha_s, &dec_s);

      tha = (ha_s * HA_2_STEPS_A / HA_2_STEPS_B) % HA_STEPS;  // (((uint64_t)ha_s * HA_STEPS) / HA_TOTAL_SECONDS) % HA_STEPS;
      tdec =  dec_s * DEC_2_STEPS_A / DEC_2_STEPS_B;           // ((int64_t)dec_s * DEC_STEPS) / DEC_TOTAL_SECONDS;
      
      set_target_position(tha, tdec);
      print_ok();
      return;
    }
    case 'P': // Print position
    {
      print_pos(ha, dec);
      return;
    }
    case 'W': // Set dec dir orientation
    {
      read_bool(&dec_orientation);
      return;
    }
    default:  // Error
      return;
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


int main(void)
{
  UCSRB = 1 << RXCIE | 1 << TXCIE | 1 << RXEN | 1 << TXEN;
  UBRRH = UBRR_VALUE >> 8;
  UBRRL = UBRR_VALUE & 0xFF;
  
  DDRD = 1 << DECSTEP | 1 << DECDIR | 1 << HASTEP | 1 << HADIR;
  DDRB = 1 << 5;
  
  ENABLE_HA_DDR  |= 1 << ENABLE_HA_PIN;
  ENABLE_DEC_DDR |= 1 << ENABLE_DEC_PIN;

  memset(inbuf, 0, sizeof(inbuf));
  memset(outbuf, 0, sizeof(outbuf));

  sei();

  static const uint32_t siderial_sync_ha_speed = (86400 / 86164.090530833) * 3600 * SUBSECONDS;
  set_secs_per_hour(siderial_sync_ha_speed, 0);
  dec_orientation = false;

  while (true)
  {
    if (inbuf_rdy)
    {
      handle_command();
      memset(inbuf, 0, sizeof(inbuf));
      inpos = 0;
      inbuf_rdy = false;
    }
  }
  return 0;
}
