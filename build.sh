#!/bin/sh

avr-gcc -mmcu=attiny4313 main.c -o main.elf -Os -DF_CPU=18000000UL -DDEBUG=0
#avr-gcc -mmcu=attiny2313a main.c -o main.elf -Os -DF_CPU=18000000UL
avr-objcopy -O binary main.elf main.bin
#avrdude -c usbasp -p m328p -U flash:w:main.bin

