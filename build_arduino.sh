#!/bin/sh

avr-gcc -mmcu=atmega328 main.c -o main.elf -Os -DARDUINO -DF_CPU=16000000UL
avr-objcopy -O binary main.elf main.bin
avrdude -c usbasp -p m328p -U flash:w:main.bin
