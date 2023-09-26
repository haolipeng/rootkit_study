obj-m += module_lkm.o
obj-m += module_monitor.o
 
all: module_lkm.ko module_monitor.ko

module_lkm-objs := lkm.o
module_monitor-objs := monitor.o

KDIR    := /lib/modules/$(shell uname -r)/build

%.ko:
	make -C $(KDIR) M=$(shell pwd) modules

clean:
	make -C $(KDIR) M=$(shell pwd) clean
