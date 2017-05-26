CWFLAGS = -Wall -Wextra -Wformat -pedantic -Wshadow -Wno-format -std=c99
CFLAGS = -O3 -mtune=core2 -march=core2 -mstackrealign -flto -ffat-lto-objects -fomit-frame-pointer -fno-ident -fgraphite -municode
LDFLAGS = -s -static -fno-ident -municode
#CFLAGS = -Og -g3 -ggdb -gdwarf-3 -fno-omit-frame-pointer -fvar-tracking-assignments -fno-ident -municode
#LDFLAGS = -fno-ident -static -municode
PATHS = 
LIBS = 
BINEXT = .exe

ifeq (, $(findstring __MINGW64__, $(shell $(CC) -dM -E - </dev/null 2>/dev/null)))
 # patch to handle missing symbols in mingw32 correctly
 CFLAGS += -D__MINGW64__=1
endif
