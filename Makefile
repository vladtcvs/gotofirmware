TARGET="attiny4313"

ifeq ($(TARGET), "attiny4313")
CFLAGS=-Os -DF_CPU=18000000UL -DDEBUG=0 -mmcu=attiny4313
MCU=t4313
else ifeq ($(TARGET), "atmega328")
CFLAGS=-mmcu=atmega328 -Os -DARDUINO -DF_CPU=16000000UL
MCU=m328p
else

endif

all: firmware.bin
compile: firmware.bin

firmware.bin: firmware.elf
	avr-objcopy -O binary $< $@

firmware.elf: main.c
	avr-gcc $(CFLAGS) main.c -o $@

flash: firmware.bin
	avrdude -c usbasp -p $(MCU) -U flash:w:$<

clean:
	rm -f firmware.bin firmware.elf

