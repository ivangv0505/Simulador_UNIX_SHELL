CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -O2 -Iinclude

# Objetos comunes
COMMON_OBJS = bin/config.o bin/logging.o bin/instance.o bin/pager.o bin/remote_client.o bin/remote_server.o

# uamashell
UAMASHELL_OBJS = $(COMMON_OBJS) bin/uamashell.o
bin/uamashell: $(UAMASHELL_OBJS)
	$(CC) $(CFLAGS) $(UAMASHELL_OBJS) -o $@ -lncursesw

# test_main
TEST_OBJS = $(COMMON_OBJS) bin/test_main.o
bin/test_main: $(TEST_OBJS)
	$(CC) $(CFLAGS) $(TEST_OBJS) -o $@

# Reglas para compilar cada .o
bin/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f bin/*.o bin/uamashell bin/test_main

