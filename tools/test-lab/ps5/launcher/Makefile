# ps5-native-app-boilerplate - standalone developer launcher.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
ifndef PS5_PAYLOAD_SDK
$(error Set PS5_PAYLOAD_SDK to an installed payload SDK)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
# 0: direct, size zero; 1: direct, explicit size; 2: existing PSFWD loopback; 3: preflight only; 4: staged --test-run (PR1).
PR0_ROUTE ?= 0
ifeq ($(filter $(PR0_ROUTE),0 1 2 3 4),)
$(error PR0_ROUTE must be 0, 1, 2, 3 or 4)
endif
.PHONY: all clean
all: test-launcher-route$(PR0_ROUTE).elf
	cp $< test-launcher.elf
test-launcher-route$(PR0_ROUTE).elf: main.c protocol.h loopback.h Makefile
	$(CC) -DPR0_ROUTE=$(PR0_ROUTE) -DPLTEST_KERNEL_FW=1 -Wall -Wextra -Werror -O2 -o $@ main.c -lSceSystemService -lSceUserService
clean:
	rm -f test-launcher.elf test-launcher-route*.elf
