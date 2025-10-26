ThaweCode: ThaweCode.c syntax.c config.c
	$(CC) ThaweCode.c syntax.c config.c -o ThaweCode -Wall -Wextra -pedantic -std=c99 -lncurses
