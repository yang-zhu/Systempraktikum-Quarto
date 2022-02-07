CC = gcc
CFLAGS = -Wall -Werror -Wextra -g -pthread

all: sysprak-client

play: sysprak-client
	./sysprak-client -g $(GAME_ID) -p $(PLAYER)

sysprak-client: sysprak-client.o connector.o thinker.o config.o utils.o sharedMemory.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c *.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f sysprak-client *.o