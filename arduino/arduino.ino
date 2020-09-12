
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

#define BAUD 9600

#define UBRR_VALUE F_CPU / 16 / BAUD - 1

/* Mount options */
#define DEC_TOTAL_SECONDS            ((int32_t)(360.0*60*60))
#define DEC_STEPPER_ANGLE            1.8
#define DEC_STEPPER_MICROSTEP        2
#define DEC_STEPPER_STEPS            (360.0 * DEC_STEPPER_MICROSTEP / DEC_STEPPER_ANGLE)
#define DEC_GEAR_SMALL               20
#define DEC_GEAR_BIG                 40
#define DEC_MOUNT_REDUCTION_NUMBER   144.0
#define DEC_REDUCTION_NUMBER         (DEC_MOUNT_REDUCTION_NUMBER * DEC_GEAR_BIG / DEC_GEAR_SMALL)
#define DEC_STEPS                    ((int32_t)(DEC_STEPPER_STEPS * DEC_REDUCTION_NUMBER))

// DEC_2_STEPS_A / DEC_2_STEPS_B = DEC_STEPS / DEC_TOTAL_SECONDS
#define DEC_2_STEPS_A                4
#define DEC_2_STEPS_B                45

#define HA_TOTAL_SECONDS            ((uint32_t)(24.0*60*60))
#define HA_STEPPER_ANGLE            1.8
#define HA_STEPPER_MICROSTEP        2
#define HA_STEPPER_STEPS            (360.0 * HA_STEPPER_MICROSTEP / HA_STEPPER_ANGLE)
#define HA_GEAR_SMALL               20
#define HA_GEAR_BIG                 40
#define HA_MOUNT_REDUCTION_NUMBER   144.0
#define HA_REDUCTION_NUMBER         (HA_MOUNT_REDUCTION_NUMBER * HA_GEAR_BIG / HA_GEAR_SMALL)
#define HA_STEPS                    ((uint32_t)(HA_STEPPER_STEPS * HA_REDUCTION_NUMBER))

// HA_2_STEPS_A / HA_2_STEPS_B = HA_STEPS / HA_TOTAL_SECONDS
#define HA_2_STEPS_A                4
#define HA_2_STEPS_B                3


#pragma message "DEC STEPS: " XSTR(DEC_STEPS) " MICROSTEP: " XSTR(DEC_STEPPER_MICROSTEP)
#pragma message "HA STEPS: " XSTR(HA_STEPS) " MICROSTEP: " XSTR(HA_STEPPER_MICROSTEP)

#define SUBSECONDS                  2

#define FULL_ROTATION_TIME_SECONDS  180
#define GOTO_HA_STEPS_PER_SECOND    (HA_STEPS / FULL_ROTATION_TIME_SECONDS)
#define GOTO_DEC_STEPS_PER_SECOND   (DEC_STEPS / FULL_ROTATION_TIME_SECONDS)

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
volatile bool transmit = false;

// HA mode vars

uint32_t delay_ha;          // Delay between steps for H.A.
uint32_t counter_ha;

uint32_t delay_dec;         // Delay between steps for Dec.
uint32_t counter_dec;

int32_t  ha_speed;          // H.A. speed in H mode
//int32_t  dec_speed;         // Dec. speed in H mode

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

void print_ok(void)
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
void print_debug(const char *str)
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

void print_pos(uint32_t ha, int32_t dec)
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

void set_secs_per_hour(int32_t ha_secs_per_hour);

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
    set_secs_per_hour(ha_speed);
  }
}

static void tick_movement(void)
{
  step_ha();
}

ISR(TIMER1_COMPA_vect)
{
  blink();
  if (isgoto)
  { 
    make_step_goto();
  }
  else
  {
    tick_movement();
  }
}

ISR(TIMER1_COMPB_vect)
{
  clear_steps();
}

// secs_per_hour in fixed point format
void set_secs_per_hour(int32_t ha_secs_per_hour)
{
  #define TIMER_PRESCALER_HA 1024
  #define PSC2_HA 1
  #define PSC1_HA 0
  #define PSC0_HA 1

  cli();
  
  ha_speed = ha_secs_per_hour;
  //dec_speed = dec_secs_per_hour;
  
  SDPORT &= ~(1<<ENABLE);
  isgoto = false;
  
  if (ha_secs_per_hour == 0)
  {
     TCCR1B = (0 << CS12) | (0 << CS11) | (0 << CS10) | (1 << WGM12) | (0 << WGM13); // disabled, CTC
     sei();
     return;
  }

  dir_ha(ha_secs_per_hour >= 0);
  //dir_dec(dec_secs_per_hour >= 0);
  
  ha_secs_per_hour = abs(ha_secs_per_hour);
  //dec_secs_per_hour = abs(dec_secs_per_hour);

  /*  
   * timer tick frequency = F_CPU / TIMER_PRESCALER
   * timer tick delta t = TIMER_PRESCALER / F_CPU
   * 
   * H.A. seconds per hour = secs_per_hour / SUBSECONDS
   * H.A. seconds per second = secs_per_hour / SUBSECONDS / 3600
   * 
   * H.A. steps per second  = "H.A. seconds per second" * HA_STEPS / HA_SECONDS = 
   *                        = "H.A. seconds per second" * HA_2_STEPS_A / HA_2_STEPS_B =
   *                        = secs_per_hour * HA_2_STEPS_A / SUBSECONDS / 3600 / HA_2_STEPS_B
   * 
   * step delta t = 1 / "H.A. steps per second"
   * counter = "step delta t" / "timer tick delta t" =
   *         = 1 / "H.A. steps per second" / (TIMER_PRESCALER / F_CPU) = 
   *         = SUBSECONDS * 3600 * HA_2_STEPS_B  * F_CPU / TIMER_PRESCALER / secs_per_hour / HA_2_STEPS_A
   *
   *
   * secs_per_hour / SUBSECONDS = 3600 * HA_2_STEPS_B  * F_CPU / TIMER_PRESCALER / counter / HA_2_STEPS_A. 
   * 32 <= counter <= 65535
   * 724 <= secs_per_hour / SUBSECONDS <= 1483154
   *
   */

  const uint32_t HA_A  = (uint64_t)3600 * HA_2_STEPS_B * SUBSECONDS * F_CPU / TIMER_PRESCALER_HA / HA_2_STEPS_A;
//  const uint32_t DEC_A = (uint64_t)3600 * DEC_2_STEPS_B * SUBSECONDS * F_CPU / TIMER_PRESCALER_DEC / DEC_2_STEPS_A;

  if (ha_secs_per_hour != 0)
    delay_ha = HA_A / ha_secs_per_hour;
  else
    delay_ha = 0;

#if DEBUG
  {
    sei();
    char buf[100] = {0};
    snprintf(buf, 100, "delay %ld %ld", (long)delay_ha, (long)delay_dec);
    print_debug(buf);
    cli();
  }
#endif

  counter_ha = 0;
  counter_dec = 0;

  TCCR1A = (0 << WGM11) | (0 << WGM10);
  TCCR1C = 0;
  OCR1A = delay_ha;
  OCR1B = 2;
  TIMSK = (1 << OCIE1A) | (1 << OCIE1B);
  TCCR1B = (PSC2_HA << CS12) | (PSC1_HA << CS11) | (PSC0_HA << CS10) | (1 << WGM12) | (0 << WGM13); // pre-scaler 1024, CTC
  TCNT1 = 0;
  
  #undef TIMER_PRESCALER_HA
  #undef PSC2_HA
  #undef PSC1_HA
  #undef PSC0_HA

  sei();
}

void set_target_position(uint32_t tha, int32_t tdec)
{
  #define TIMER_PRESCALER 64
  #define PSC2 0
  #define PSC1 1
  #define PSC0 1

  cli();

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

  SDPORT &= ~(1<<ENABLE);

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
    counter = (uint32_t)(F_CPU / TIMER_PRESCALER / GOTO_HA_STEPS_PER_SECOND);
  }
  else
  {
    // use Dec. as main axis
    delta_x = ddec;
    delta_y = dha;
    ha_is_x = false;
    counter = (uint32_t)(F_CPU / TIMER_PRESCALER / GOTO_DEC_STEPS_PER_SECOND);
  }

  if (delta_x == 0)
  {
    sei();
    return;
  }
  x = 0;
  error = 0;
  delta_error = delta_y + 1;

  /*
   * step delta t = 1 / GOTO_HA_STEPS_PER_SECOND
   * timer tick delta t = TIMER_PRESCALER / F_CPU
   * counter = "step delta t" / "timer tick delta t"
   */
  
  if (counter >= 65536UL)
    counter = 65535U;

  isgoto = true;
  
  TCCR1A = (0 << WGM11) | (0 << WGM10);
  TCCR1C = 0;
  OCR1A = counter;
  OCR1B = 2;
  TIMSK = (1 << OCIE1A) | (1 << OCIE1B);
  TCCR1B = (PSC2 << CS12) | (PSC1 << CS11) | (PSC0 << CS10) | (1 << WGM12) | (0 << WGM13); // pre-scaler 64, CTC
  TCNT1 = 0;
  sei();
  
  #undef TIMER_PRESCALER
  #undef PSC2
  #undef PSC1
  #undef PSC0

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
      set_secs_per_hour(0);
      PORTD |= 1<<ENABLE;
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
      int32_t ha_secs_per_hour;
      read_int(&ha_secs_per_hour);
      set_secs_per_hour(ha_secs_per_hour);
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
