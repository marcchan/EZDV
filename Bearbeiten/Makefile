################################################################################
# MODULE_NAME muss ungleich SOURCES sein!
MODULE_NAME = control_module

OBJS		= Beispielprojekt.o
SOURCES		= Beispielprojekt.c
################################################################################

KERNEL_DIR				:= /usr/src/linux-headers-2.6.38.8rtai
INCLUDES 				:= -I/usr/src/linux-headers-2.6.38.8rtai/include -I/usr/src/linux-headers-2.6.38.8rtai/arch/x86/include -I/usr/realtime/include -I/usr/include -I/usr/include/i386-linux-gnu -I/usr/local/rtnet/include 
LIBS					:= 
LFLAGS					:= 
SYMBOLS 				:= /usr/realtime/Module.symvers /usr/share/modbus-com/Module.symvers /usr/local/rtnet/Module.symvers 
EXTRA					:= -O2 -Wall

KBUILD_EXTRA_SYMBOLS	:= $(SYMBOLS)
EXTRA_CFLAGS			+= $(INCLUDES) $(EXTRA) $(LIBS)
obj-m					+= $(MODULE_NAME).o
$(MODULE_NAME)-objs		:= $(OBJS)

all:
	$(MAKE) KBUILD_VERBOSE=3 -C $(KERNEL_DIR) SUBDIRS=$(PWD) modules

clean:
	rm -rf .tmp_versions *.symvers *.o *.ko *.mod.c .*.cmd .*flags *.order
