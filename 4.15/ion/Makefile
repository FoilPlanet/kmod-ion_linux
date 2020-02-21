# 
# Adnroid ION driver for linux and android container
#
# Copyright (c) 2019-2020 FoilPlanet. All rights reserved.
#
#

ccflags-y += -I$(src) -Wno-error=implicit-int -Wno-int-conversion -Wno-error=implicit-function-declaration -Wno-pointer-to-int-cast
obj-m := ion_linux.o

ion_linux-y := ion.o ion-ioctl.o ion_heap.o \
    ion_system_heap.o ion_page_pool.o \
    ion_carveout_heap.o ion_chunk_heap.o \
    ion_device.o
#   ion_cma_heap.o ion_device.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNEL_SRC) V=0 M=$$PWD

install:
	cp ion_linux.ko $(DESTDIR)/

clean:
	@rm -rf deps.h *.o *.ko *.mod.c *.symvers *.order .*.cmd
	@rm -rf .tmp_versions .cache.mk *.o.ur-safe


