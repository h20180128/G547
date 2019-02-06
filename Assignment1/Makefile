obj-m := main2.o



all:

	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules



insert: main.ko

	sudo insmod main.ko



remove: main.ko

	sudo rmmod main.ko



compile: user.c

	gcc user.c -o user



clean:

	rm -rf *.o *.order *.ko *.mod *.symvers *.mod.c
