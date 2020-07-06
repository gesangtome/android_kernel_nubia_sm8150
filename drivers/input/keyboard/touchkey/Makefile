# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the input core drivers.
#

# Each configuration option enables a list of files.
obj-$(CONFIG_NUBIA_CYPRESS_KEYBOARD_GAMEFIRE) += cy8c4014l_touchkey_firmware_update.o cybtldr_touchkey_command.o cy8c4014l_touchkey.o

ifeq ($(TARGET_NUBIA_NX629J_V1S), true)
    ccflags-y += -DTARGET_NUBIA_NX629J_V1S
endif
