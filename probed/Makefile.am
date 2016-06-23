bin_PROGRAMS = probed 
probed_SOURCES = client.c loop.c main.c net.c tstamp.c unix.c util.c
probed_CFLAGS = $(XML2_CFLAGS) -Wall -pedantic -fno-stack-protector
probed_LDADD = $(XML2_LIBS) -lrt
#probed_LDFLAGS = -pg
