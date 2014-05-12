#
# kjscal - A module that provides automatic joystick calibration
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


default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
endif

kjscal.c: version.h

$(MODULE).ko: default

clean:
	rm -rf *.o .*.o.cmd .*.ko.cmd .tmp* *.mod.c *.ko

install: $(MODULE).ko
	install -m644 -D $(MODULE).ko $(IDIR)/$(MODULE).ko
	depmod -a -r $(KVER)

uninstall:
	rm -f $(IDIR)/$(MODULE).ko
