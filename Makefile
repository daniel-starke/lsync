PREFIX = 
CC = $(PREFIX)gcc

SRC = \
  src/argps.c \
  src/argpus.c \
  src/getopt.c \
  src/lsync.c \
  src/tchar.c \
  src/tdirs.c \
  src/tdirus.c

SYS := $(shell $(CC) -dumpmachine)
ifneq (, $(findstring linux, $(SYS)))
 include src/linux.mk
else
 ifneq (, $(findstring mingw, $(SYS)))
  include src/mingw.mk
 else
  include src/general.mk
 endif
endif

all: bin bin/lsync$(BINEXT)

.PHONY: clean
clean:
	rm -f bin/lsync$(BINEXT)

bin:
	mkdir bin

.PHONY: bin/lsync$(BINEXT)
bin/lsync$(BINEXT): $(SRC)
	rm -f $@
	$(CC) $(CFLAGS) $(CWFLAGS) $(PATHS) $(LDFLAGS) -o $@ $+ $(LIBS)
