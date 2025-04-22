all: threadlib main
	gcc -o main main.o threads.o -Werror -Wall -g -std=gnu99

main: main.c
	gcc -c -o main.o main.c -c -Werror -Wall -g -std=gnu99

threadlib: threads.c
	gcc -c -o threads.o threads.c -c -Werror -Wall -g -std=gnu99

clean:
	rm threads.o main.o main
