thawe_code: thawe_code.c syntax.c config.c
	$(CC) thawe_code.c syntax.c config.c -o thawe_code -Wall -Wextra -pedantic -std=c99 -lncurses
