#
# kjscal - A module that provides automatic joystick calibration
#
# This module creates for each joystick a virtual joystick with
# automatic axis calibration.
#
# Copyright (c) 2005 Theodoros V. Kalamatianos <nyb@users.sourceforge.net>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published by
# the Free Software Foundation.
#

MODULE := kjscal

ifneq ($(KERNELRELEASE),)

obj-m := $(MODULE).o

else

KVER := $(shell uname -r)
KDIR := /lib/modules/$(KVER)/build
IDIR := /lib/modules/$(KVER)/misc
PWD  := $(shell pwd)
VERSION := $(shell cat VERSION)


default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) EXTRA_CFLAGS=-DVERSION=\\\"$(VERSION)\\\" modules
endif

clean:
	rm -rf *.o .*.o.cmd .*.ko.cmd .tmp* *.mod.c *.ko

$(MODULE).ko: default

install: $(MODULE).ko
	install -m644 -D $(MODULE).ko $(IDIR)/$(MODULE).ko
	depmod -a -r $(KVER)

uninstall:
	rm -f $(IDIR)/$(MODULE).ko
