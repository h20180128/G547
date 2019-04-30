Block Device Driver
>Download the main.c ,Makefile and Readme.md

>Go to the directory in which code is downloaded and give the command $ make all

>insert the module using sudo insmod main.ko

>check if module is loaded using lsmod command

>Check the partition info using sudo fdisk -l /dev/dor


>2 logical partions are created

>to check mbr details using sudo hd -n 512 /dev/dor

>Take root access using sudo -s command 

>to write into disk use cat > /dev/dor1 ,type something & press enter
to read back from the disk on command line use command xxd /dev/dor1 | less


>remove the module using sudo rmmod main.ko
