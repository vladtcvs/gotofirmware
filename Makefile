ifeq (${TARGET},)
TARGET := arduino
else
TARGET := ${TARGET}
endif

ARDUINO_PORT=/dev/ttyUSB0

CC = avr-gcc

ifeq ($(TARGET), attiny4313)
CFLAGS=-Os -mmcu=attiny4313
MCU=t4313
else ifeq ($(TARGET), arduino)
CFLAGS=-mmcu=atmega328 -Os -DARDUINO -DDEBUG=1
MCU=m328p
else
endif

SRCS =		src/main.c		\
			src/shell.c		\
			src/control.c 	\
			src/command.c	\
			src/steppers.c	\
			src/timer.c

HEADERS =	src/config.h	\
			src/platform.h	\
			src/err.h 		\
			src/control.h	\
			src/command.h	\
			src/shell.h		\
			src/steppers.h	\
			src/timer.h

CONFIG=configs/config.$(TARGET).yaml

OBJS := $(SRCS:%.c=%.o)

all: firmware.bin
compile: firmware.bin

src/config.h: $(CONFIG) build_config.py
	python3.8 build_config.py $< $@

config: src/config.h

%.o : %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

firmware.elf: $(OBJS)
	avr-gcc $(CFLAGS) $^ -o $@

firmware.bin: firmware.elf
	avr-objcopy -O binary $< $@

flash: firmware.bin
	avrdude -c usbasp -p $(MCU) -U flash:w:$<

flash_arduino: firmware.bin
	avrdude -carduino -p $(MCU) -U flash:w:$< -P $(ARDUINO_PORT)
	

clean:
	rm -f firmware.bin firmware.elf $(OBJS)

config.h: config.yaml build_config.py
	python3 build_config.py config.yaml $@

