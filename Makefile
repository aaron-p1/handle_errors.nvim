CC = gcc
CFLAGS = -Wall -Wextra -fPIC $(shell pkg-config --cflags luajit)
LDFLAGS = -shared -ldl

SRC = ./src/override_error_printing.c
DST = ./lua/handle_errors/override_error_printing.so

$(DST): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f $(DST)
