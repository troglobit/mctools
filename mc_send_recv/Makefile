
CFLAGS = -I../../../src -Wall -Werror
EXEC = multiblast mcreceive
RM   = -rm -f 

all: $(EXEC)

dbc_multiblast.c: multiblast.c
	../../dbcc/dbcc multiblast.c

multiblast: dbc_multiblast.c
	$(CC) $(CFLAGS) -o $@ dbc_multiblast.c

mcreceive: mcreceive.c

clean:
	$(RM) $(EXEC) dbc_*.c
