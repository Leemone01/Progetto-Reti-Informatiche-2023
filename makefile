all: cli td kd server

cli: cli.o
	gcc -Wall cli.o -o cli

td: td.o
	gcc -Wall td.o -o td

kd: kd.o
	gcc -Wall kd.o -o kd

server: server.o
	gcc -Wall server.o -o server

clean:
	rm *o cli td kd server
