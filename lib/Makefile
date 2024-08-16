SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
LIB = libktls.a 

REQ = openssl

# TODO: Remove this if you want to use a OpenSSL library on your OS.
export PKG_CONFIG_PATH := ../../openssl/build/lib64/pkgconfig

CFLAGS  += -Wall -Werror -g -O2 -I../include

.PHONY: all clean lint

all: $(LIB)

$(LIB): $(OBJ)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags --libs openssl) -c -o $@ $^

lint:

clean:
	rm -rf $(OBJ) $(LIB)
