#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#include "steppers.h"
#include "control.h"
#include "shell.h"
#include "err.h"

static size_t read_int_octal(const char *inbuf, size_t len, int32_t *val)
{
	int sign = 1;
	uint8_t i = 0;
  	while (i < len && inbuf[i] == ' ')
    	i++;

	*val = 0;
	if (i >= len)
	{
		return i;
	}

	if (inbuf[i] == '+')
	{
		i++;
	}

	if (inbuf[i] == '-')
	{
		i++;
		sign = -1;
	}

  	while (i < len && inbuf[i] >= '0' && inbuf[i] <= '7')
  	{
		*val *= 8;
		*val += inbuf[i]-'0';
		i++;
  	}
	*val *= sign;
  	return i;
}

void handle_command(const char *inbuf, size_t len)
{
  if (len == 0)
    return;
  switch (inbuf[0])
  {
    case 'D': // Disable steppers
    {
		control_stop_moving();
		steppers_disable();
      	shell_print_ok();
      	return;
    }
    case 'S': // Set current position, x, y
    {
		int32_t x;
		int32_t y;
		uint32_t off;
		inbuf++;
		len--;

		off = read_int_octal(inbuf, len, &x);
		inbuf += off;
		len -= off;
		off = read_int_octal(inbuf, len, &y);
		inbuf += off;
		len -= off;

		control_stop_moving();

		uint32_t pos[2] = {x, y};
		control_set_current_position(pos);
		shell_print_ok();
		return;
    }
    case 'G': // Goto "H.A. Dec" - delta
    {
		int32_t tid;
		int32_t x;
		int32_t y;
		uint32_t off;
		uint32_t delay;
		inbuf++;
		len--;

		off = read_int_octal(inbuf, len, &tid);
		inbuf += off;
		len -= off;
		off = read_int_octal(inbuf, len, &x);
		inbuf += off;
		len -= off;
		off = read_int_octal(inbuf, len, &y);
		inbuf += off;
		len -= off;
		off = read_int_octal(inbuf, len, &delay);
		inbuf += off;
		len -= off;

		int32_t dpos[2] = {x, y};
		if (control_add_target_delta(tid, dpos, delay))
		{
			shell_print_ok();
		}
		else
		{
			shell_print_err(ERR_QUEUE_FULL);
		}
		return;
    }
    case 'P': // Print position
    {
		uint8_t tid;
		int32_t pos[2];
      	control_current_position(&tid, pos);
		shell_print_pos(tid, pos[0], pos[1]);
      	return;
    }
    default:  // Error
      return;
  }
}
