all: fs.o

main: fs.o disk.o
	gcc -std=c99 -Wall -g $^ main.c -o main

fs: fs.o disk.o
	gcc -std=c99 -Wall -g $^ -o fs

%.o: %.c
	gcc -std=c99 -Wall -g -c $< -o $@

clean:
	rm -f main fs *.o *~