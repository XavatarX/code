
CFLAGS	= -I../include  -L../common
LIBS	= ../common/libcommon.a
all:spc_lru

spc_lru:
	gcc -g -I../include  -L../common    spc_lowmem_lru.c ../common/libcommon.a  -o spc_lowmem_lru
	

clean:
	@rm -rf spc_lru
