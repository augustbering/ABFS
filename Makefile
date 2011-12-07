CPPFLAGS := -g3  -fmessage-length=0 -D_FILE_OFFSET_BITS=64 -Wall $(shell pkg-config fuse --cflags)
LDFLAGS := $(shell pkg-config fuse --libs) -llzo2 -lboost_filesystem -lboost_program_options -lboost_thread
OBJS := Block.o abfile.o  abfs.o
targets = abfs
CC = g++
DEBUG := -g3

all: $(targets)
	
abfs:  $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

fusexmp_fh: fusexmp_fh.c
	$(CC) $(CFLAGS) $(LDFLAGS) -lulockmgr $< -o $@

clean:
	rm -f *.o
	rm -f $(targets)

