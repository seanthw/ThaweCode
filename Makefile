kilo: kilo.c syntax.c config.c
	$(CC) kilo.c syntax.c config.c -o kilo -Wall -Wextra -pedantic -std=c99 -lncurses
