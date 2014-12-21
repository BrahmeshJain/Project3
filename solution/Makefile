KDIR:= ~/SDK/sysroots/i586-poky-linux/usr/src/kernel
CC = i586-poky-linux-gcc
ARCH = x86
CROSS_COMPILE = i586-poky-linux-
SROOT=~/SDK/sysroots/i586-poky-linux/usr/src/kernel

APP = spi_led

obj-m:= spi_led.o pulse.o

all:
	make ARCH=x86 CROSS_COMPILE=i586-poky-linux- -C $(KDIR) M=$(PWD) modules
	
clean:
	rm -f *.ko
	rm -f *.o
	rm -f Module.symvers
	rm -f modules.order
	rm -f *.mod.c
	rm -rf .tmp_versions
	rm -f *.mod.c
	rm -f *.mod.o
	rm -f \.*.cmd
	rm -f Module.markers
	rm -f $(APP) 
	rm -f *.log

cleanlog:
	rm -f *.log
