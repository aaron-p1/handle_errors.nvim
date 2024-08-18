CC = gcc
CFLAGS = -Wall -Wextra -fPIC
LDFLAGS = -shared -ldl

SRC = ./src/override_nlua_error.c
DST = ./lua/handle_lua_errors/override_nlua_error.so

$(DST): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f $(DST)
