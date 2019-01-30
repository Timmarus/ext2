
CFLAGS=-std=gnu99 -Wall -g

all: clean ext2_rm_bonus ext2_checker ext2_cp ext2_ln ext2_restore ext2_rm ext2_mkdir

ext2_rm_bonus : ext2_helpers.o
	gcc $(CFLAGS) -o ext2_rm_bonus ext2_rm_bonus.c ext2_helpers.c ext2_helpers.h

ext2_checker : ext2_helpers.o
	gcc $(CFLAGS) -o ext2_checker ext2_checker.c ext2_helpers.c ext2_helpers.h

ext2_cp : ext2_helpers.o
	gcc $(CFLAGS) -o ext2_cp ext2_cp.c ext2_helpers.c ext2_helpers.h

ext2_ln : ext2_helpers.o
	gcc $(CFLAGS) -o ext2_ln ext2_ln.c ext2_helpers.c ext2_helpers.h

ext2_restore : ext2_helpers.o
	gcc $(CFLAGS) -o ext2_restore ext2_restore.c ext2_helpers.c ext2_helpers.h

ext2_rm : ext2_helpers.o
	gcc $(CFLAGS) -o ext2_rm ext2_rm.c ext2_helpers.c ext2_helpers.h

ext2_mkdir : ext2_helpers.o
	gcc $(CFLAGS) -o ext2_mkdir ext2_mkdir.c ext2_helpers.c ext2_helpers.h

%.o : %.c ext2_helpers.h ext2.h
	gcc $(CFLAGS) -c $<

clean : 
	rm -f *.o *~
