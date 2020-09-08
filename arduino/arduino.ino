
#ifndef DEBUG
#define DEBUG 0
#endif

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

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

#define INBUFLEN 100
#define OUTBUFLEN 100

#else

#define INBUFLEN 20
#define OUTBUFLEN 20

#endif

#define BAUD 9600

#define UBRR_VALUE F_CPU / 16 / BAUD - 1

/* Mount options */
#define DEC_STEPPER_ANGLE            1.8
#define DEC_STEPPER_MICROSTEP        16
#define DEC_STEPPER_STEPS            (360.0 * DEC_STEPPER_MICROSTEP / DEC_STEPPER_ANGLE)
#define DEC_GEAR_SMALL               20
#define DEC_GEAR_BIG                 40
#define DEC_MOUNT_REDUCTION_NUMBER   144.0
#define DEC_REDUCTION_NUMBER         (DEC_MOUNT_REDUCTION_NUMBER * DEC_GEAR_BIG / DEC_GEAR_SMALL)
#define DEC_STEPS                    ((int32_t)(DEC_STEPPER_STEPS * DEC_REDUCTION_NUMBER))

#define HA_STEPPER_ANGLE            1.8
#define HA_STEPPER_MICROSTEP        16
#define HA_STEPPER_STEPS            (360.0 * HA_STEPPER_MICROSTEP / HA_STEPPER_ANGLE)
#define HA_GEAR_SMALL               20
#define HA_GEAR_BIG                 40
#define HA_MOUNT_REDUCTION_NUMBER   144.0
#define HA_REDUCTION_NUMBER         (HA_MOUNT_REDUCTION_NUMBER * HA_GEAR_BIG / HA_GEAR_SMALL)
#define HA_STEPS                    ((uint32_t)(HA_STEPPER_STEPS * HA_REDUCTION_NUMBER))

#define DEC_TOTAL_SECONDS           ((int32_t)(360.0*60*60))
#define HA_TOTAL_SECONDS            ((uint32_t)(24.0*60*60))

#define FULL_ROTATION_TIME_MIN      3
#define GOTO_SPEED_SECS_PER_HOUR    (3600UL * 24 * 60 / (FULL_ROTATION_TIME_MIN))

/* Step/dir */

// PORTD
#define SDPORT                      PORTD
#define HADIR                       4
#define HASTEP                      3
#define DECDIR                      6
#define DECSTEP                     5
#define ENABLE                      2

/* For arduino prototype */
#ifdef ARDUINO
#define UDR UDR0
#define UCSRA UCSR0A
#define UCSRB UCSR0B
#define UCSRC UCSR0C

#define RXCIE RXCIE0
#define TXCIE TXCIE0
#define RXEN RXEN0
#define TXEN TXEN0

#define TIMSK TIMSK1

#define UBRR UBRR0
#define UBRRL UBRR0L
#define UBRRH UBRR0H

#define UDRE UDRE0

#else

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

bool transmit = false;

// HA mode vars

uint16_t ha_delay_counter;  // Delay between steps for H.A.
int32_t  ha_speed;          // H.A. speed in H mode

// GOTO mode vars

uint32_t delta_x, x;
uint32_t delta_y;
uint32_t error, delta_error;

bool ha_is_x;

// COMMON vars

volatile bool isgoto;     // system in goto mode

uint32_t ha;              // in steps
int32_t dec;              // in steps

bool ha_pos_dir;          // H.A. step direction
bool dec_pos_dir;         // Dec. step direction

int32_t atoi32(const char *s)
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

void itoa32(int32_t val, char *s)
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

#if DEBUG
void print_debug(const char *str)
{
  memset(outbuf, 0, OUTBUFLEN);
  strcpy(outbuf, str);
  int len = strlen(outbuf);
  outbuf[len++] = '\r';
  outbuf[len++] = '\n';
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

void print_pos(uint32_t ha, int32_t dec)
{
  uint8_t i = 0;
  uint32_t ha_s = ha  * 3 / 32;   // HA_TOTAL_SECONDS / HA_STEPS;
  int32_t dec_s = dec * 45 / 32;  // DEC_TOTAL_SECONDS / DEC_STEPS;

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

void send_nl(void)
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

void dir_ha(bool pos)
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

void dir_dec(bool pos)
{
  dec_pos_dir = pos;
  if (pos)
  {
    SDPORT &= ~(1<<DECDIR);
  }
  else
  {
    SDPORT |= (1<<DECDIR);
  }
}

void step_ha(void)
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

void step_dec(void)
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

void clear_steps(void)
{
  SDPORT &= ~(1 << HASTEP | 1 << DECSTEP);
}

void set_ha_secs_per_hour(int32_t secs_per_hour);

ISR(TIMER1_COMPA_vect)
{
  blink();
  if (isgoto)
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
      set_ha_secs_per_hour(ha_speed);
    }
  }
  else
  {
    step_ha();
  }
}

ISR(TIMER1_COMPB_vect)
{
  clear_steps();
}

void set_ha_secs_per_hour(int32_t secs_per_hour)
{
  ha_speed = secs_per_hour;
  SDPORT &= ~(1<<ENABLE);
  isgoto = false;
  
  if (secs_per_hour == 0)
  {
     TCCR1B = (0 << CS12) | (0 << CS11) | (0 << CS10) | (1 << WGM12) | (0 << WGM13); // disabled, CTC
     return;
  }

  dir_ha(secs_per_hour >= 0);
  secs_per_hour = abs(secs_per_hour);
  
  uint32_t counter = (uint32_t)((uint64_t)3600 * 3 * F_CPU / 1024 / 32) / secs_per_hour;

#if DEBUG
  {
    char buf[100];
    snprintf(buf, 100, "counter %ld %i", (unsigned long)counter, (int)ha_pos_dir);
    print_debug(buf);
  }
#endif

  if (counter >= 65536UL)
    ha_delay_counter = 65535U;
  else
    ha_delay_counter = counter;

  cli();
  TCCR1A = (0 << WGM11) | (0 << WGM10);
  TCCR1C = 0;
  OCR1A = ha_delay_counter;
  OCR1B = 2;
  TIMSK = (1 << OCIE1A) | (1 << OCIE1B);
  TCCR1B = (1 << CS12) | (0 << CS11) | (1 << CS10) | (1 << WGM12) | (0 << WGM13); // pre-scaler 1024, CTC
  TCNT1 = 0;
  sei();
}

void set_target_position(uint32_t tha, int32_t tdec)
{
  int32_t dha = (int32_t)tha - ha;
  int32_t ddec = tdec - dec;

#if DEBUG
  {
    char buf[100];
    snprintf(buf, 100, "dha = %ld", (unsigned long)dha);
    print_debug(buf);
  }
#endif

  if (dha > (int32_t)HA_STEPS / 2)
    dha -= (int32_t)HA_STEPS;
  else if (-dha > (int32_t)HA_STEPS / 2)
    dha += (int32_t)HA_STEPS;

#if DEBUG
  {
    char buf[100];
    snprintf(buf, 100, "dha fixed = %ld", (unsigned long)dha);
    print_debug(buf);
  }
#endif


  SDPORT &= ~(1<<ENABLE);

  dir_ha(dha >= 0);
  dha = (dha >= 0 ? dha : -dha);

  dir_dec(ddec >= 0);
  ddec = (ddec >= 0 ? ddec : -ddec);

  if (dha > ddec)
  {
    // use H.A. as main axis
    delta_x = dha;
    delta_y = ddec;
    ha_is_x = true;
  }
  else
  {
    // use Dec. as main axis
    delta_x = ddec;
    delta_y = dha;
    ha_is_x = false;
  }

#if DEBUG
  {
    char buf[100];
    snprintf(buf, 100, "delta_x = %ld, delta_y = %ld", (unsigned long)delta_x, (unsigned long)delta_y);
    print_debug(buf);
  }
#endif

  if (delta_x == 0)
  {
    return;
  }
  x = 0;
  error = 0;
  delta_error = delta_y + 1;
  
  uint16_t counter = (uint16_t)(((uint64_t)3600 * 3 * F_CPU / 64 / 32) / GOTO_SPEED_SECS_PER_HOUR);

  isgoto = true;
  
  cli();
  TCCR1A = (0 << WGM11) | (0 << WGM10);
  TCCR1C = 0;
  OCR1A = counter;
  OCR1B = 2;
  TIMSK = (1 << OCIE1A) | (1 << OCIE1B);
  TCCR1B = (0 << CS12) | (1 << CS11) | (1 << CS10) | (1 << WGM12) | (0 << WGM13); // pre-scaler 64, CTC
  TCNT1 = 0;
  sei();
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

bool read_pos(uint32_t *ha, int32_t *dec)
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
      set_ha_secs_per_hour(0);
      PORTD |= 1<<ENABLE;
      return;
    }
    case 'S': // Set current position, H.A. Dec
    {
      uint32_t ha_s;
      int32_t dec_s;
      read_pos(&ha_s, &dec_s);
      
      ha = (ha_s * 32 / 3) % HA_STEPS;  // (((uint64_t)ha_s * HA_STEPS) / HA_TOTAL_SECONDS) % HA_STEPS;
      dec =  dec_s * 32 / 45;           // ((int64_t)dec_s * DEC_STEPS) / DEC_TOTAL_SECONDS;
      
      return;
    }
    case 'H': // H.A. axis movement with speed
    {
      int32_t secs_per_hour;
      read_int(&secs_per_hour);
      set_ha_secs_per_hour(secs_per_hour);
      return;
    }
    case 'G': // Goto "H.A. Dec"
    {
      uint32_t ha_s, tha;
      int32_t dec_s, tdec;
      read_pos(&ha_s, &dec_s);

      tha = (ha_s * 32 / 3) % HA_STEPS;  // (((uint64_t)ha_s * HA_STEPS) / HA_TOTAL_SECONDS) % HA_STEPS;
      tdec =  dec_s * 32 / 45;           // ((int64_t)dec_s * DEC_STEPS) / DEC_TOTAL_SECONDS;
      
      set_target_position(tha, tdec);
      return;
    }
    case 'P': // Print position
    {
      print_pos(ha, dec);
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
  
  DDRD = 1 << ENABLE | 1 << DECSTEP | 1 << DECDIR | 1 << HASTEP | 1 << HADIR;
  
  memset(inbuf, 0, sizeof(inbuf));
  memset(outbuf, 0, sizeof(outbuf));

  sei();

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
