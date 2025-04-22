all: threadlib test
	gcc -o test test-threads.o threads.o -Werror -Wall -g -std=gnu99

test: test-threads.c
	gcc -c -o test-threads.o test-threads.c -Werror -Wall -g -std=gnu99

threadlib: threads.c
	gcc -c -o threads.o threads.c -Werror -Wall -g -std=gnu99

clean:
	rm -f threads.o test-threads.o test
