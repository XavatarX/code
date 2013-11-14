obj-m	:= tierfs.o
tierfs-y	:=dummy.o dentry.o file.o inode.o super.o mmap.o read_write.o 


KVERSION = $(shell uname -r)
all:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean
