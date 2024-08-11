HDR = $(wildcard *.h)
SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
EXE = $(subst .o,,$(OBJ))

SUBDIR = lib

LINTER    = cppcheck
LINTFLAGS = --enable=style -j 4

REQ = openssl
LIB = lib/libktls.a

OPENSSLDIR = ../openssl

CFLAGS  += -Wall -Werror -g -O2 -I$(OPENSSLDIR)/include -I./include
LDFLAGS += -L$(OPENSSLDIR) -lssl -lcrypto

# TODO: If you want to use a OpenSSL library on your OS.
#
#CFLAGS  += -Wall -Werror -g -O2 $(shell pkg-config --cflags $(REQ)) -I./include
#LDFLAGS += $(shell pkg-config --libs $(REQ))

.PHONY: all clean lint test $(SUBDIR)

all: $(SUBDIR) $(EXE) $(OBJ)

%:%.c $(SUBDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) $(LDFLAGS)

$(SUBDIR):
	$(MAKE) -C $@ $(MAKECMDGOALS)

lint: $(SRC) $(HDR) $(SUBDIR)
	$(LINTER) $(LINTFLAGS) $^

test:
	./run.sh

clean: $(SUBDIR)
	rm -rf $(OBJ) $(EXE)
