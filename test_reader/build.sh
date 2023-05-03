#!/bin/sh
arm-nuvoton-linux-musleabi-gcc -fno-strict-aliasing -fno-common -ffixed-r8 -msoft-float -Wformat -Wall -std=c99 libnmf.c test.c -lc -lgcc
