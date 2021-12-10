#TARGET=attiny4313
TARGET=arduino

ifeq ($(TARGET), attiny4313)
CFLAGS=-Os -DDEBUG=0 -mmcu=attiny4313
MCU=t4313
else ifeq ($(TARGET), arduino)
CFLAGS=-mmcu=atmega328 -Os -DARDUINO 
MCU=m328p
else
endif

SRCS=src/main.c
HEADERS=src/config.h

CONFIG=configs/config.$(TARGET).yaml

OBJS := $(SRCS:%.c=%.o)


all: firmware.bin
compile: firmware.bin

src/config.h: $(CONFIG)
	python3.8 build_config.py $< $@

config: src/config.h

%.o : %.c $(HEADERS)
	avr-gcc $(CFLAGS) -c $< -o $@

firmware.elf: $(OBJS)
	avr-gcc $(CFLAGS) $^ -o $@

firmware.bin: firmware.elf
	avr-objcopy -O binary $< $@

flash: firmware.bin
	avrdude -c usbasp -p $(MCU) -U flash:w:$<

clean:
	rm -f firmware.bin firmware.elf

config.h: config.yaml build_config.py
	python3 build_config.py config.yaml $@

