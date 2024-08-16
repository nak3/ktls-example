HDR = $(wildcard *.h)
SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
EXE = $(subst .o,,$(OBJ))

SUBDIR = lib

LINTER    = cppcheck
LINTFLAGS = --enable=style -j 4

REQ = openssl
LIB = lib/libktls.a

# TODO: Remove this if you want to use a OpenSSL library on your OS.
export LD_LIBRARY_PATH := ../openssl/build/lib64/
export PKG_CONFIG_PATH := ../openssl/build/lib64/pkgconfig

# TODO: Building on github action needs this. why?
LDFLAGS = -L../openssl

CFLAGS  += -Wall -Werror -g -O2 -I./include

.PHONY: all clean lint test $(SUBDIR)

all: $(SUBDIR) $(EXE) $(OBJ)

%:%.c $(SUBDIR)
	$(CC) $(CFLAGS) $(shell pkg-config --cflags --libs openssl) -o $@ $< $(LIB) $(LDFLAGS)

$(SUBDIR):
	$(MAKE) -C $@ $(MAKECMDGOALS)

lint: $(SRC) $(HDR) $(SUBDIR)
	$(LINTER) $(LINTFLAGS) $^

test:
	./run.sh

clean: $(SUBDIR)
	rm -rf $(OBJ) $(EXE)
