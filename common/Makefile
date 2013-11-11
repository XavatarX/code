all:
	cd hash_table; make
	cd lru; make
	ar rcs libcommon.a hash_table/hash.o lru/lru.o
clean:
	cd hash_table;make clean
	cd lru;make clean
	@rm -rf libcommon.a
	
