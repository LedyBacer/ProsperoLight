# ps5-native-app-boilerplate - User-invoked native controller audio probe.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
ifndef PS5_PAYLOAD_SDK
$(error Set PS5_PAYLOAD_SDK to an installed payload SDK)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
PROBE_SIGNAL ?= 0
ifeq ($(filter $(PROBE_SIGNAL),0 1),)
$(error PROBE_SIGNAL must be 0 or 1)
endif
.PHONY: all
all:
	$(CC) -Wall -Wextra -Werror -O2 -DPROBE_SIGNAL=$(PROBE_SIGNAL) main.c -o waveform-probe.elf -lSceAudioOut -lSceUserService -lScePad
