obj-m += MultiDataFlow.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

mount:
	sudo insmod MultiDataFlow.ko

unmount:
	sudo rmmod MultiDataFlow

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
