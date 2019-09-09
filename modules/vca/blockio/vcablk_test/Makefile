# Comment/uncomment the following line to disable/enable debugging
DEBUG = y


# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DVCA_BLK_DEBUG # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif

EXTRA_CFLAGS += $(DEBFLAGS)
EXTRA_CFLAGS += -I..

ifneq ($(KERNELRELEASE),)
# call from kernel build system

subdir-ccflags-y += -I$(M)
subdir-ccflags-y += -I$(M)/../

subdir-ccflags-y += -DTEST_BUILD

obj-m	:= vcablk_test.o
vcablk_test-objs += ../vcablk/vcablk.o \
		../vcablk/vcablk_disk.o \
		./hal_test/vcablk_hal_test.o

obj-m	+= vcablk_bcknd_test.o
vcablk_bcknd_test-objs += ../vcablk_bcknd/vcablk_bcknd.o \
		../vcablk_bcknd/vcablk_bcknd_disk.o \
		../vcablk_bcknd/vcablk_bcknd_media.o \
		./hal_test/vcablk_bcknd_hal_test.o

else

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)


default: ctrl
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules


ctrl:
	$(CC) -Wall vcablk_ctrl/vcablk_ctrl.c -o vcablkctrl


endif

clean:
	rm -rf vcablkctrl *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.symvers modules.order
	rm -rf ./hal_test/*.o ./hal_test/.*.cmd
	rm -rf ../vcablk/*.o ../vcablk/.*.cmd
	rm -rf ../vcablk_bcknd/*.o ../vcablk_bcknd/.*.cmd

depend .depend dep:
	$(CC) $(EXTRA_CFLAGS) -M *.c > .depend


ifeq (.depend,$(wildcard .depend))
include .depend
endif
