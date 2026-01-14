CC = gcc
CFLAGS = -Wextra -g
INCLUDE = -I./include/

all: jobCommander jobExecutorServer progDelay

jobCommander: ./src/jobCommander.c
	$(CC) $(CFLAGS) $(INCLUDE) -o ./bin/jobCommander ./src/jobCommander.c

jobExecutorServer: ./src/jobExecutorServer.c
	$(CC) $(CFLAGS) $(INCLUDE) -pthread -o ./bin/jobExecutorServer ./src/jobExecutorServer.c -lrt

progDelay: ./src/progDelay.c
	$(CC) $(CFLAGS) $(INCLUDE) -o ./bin/progDelay ./src/progDelay.c

valgrind_jobCommander: jobCommander
	valgrind --leak-check=full ./bin/jobCommander

valgrind_jobExecutorServer: jobExecutorServer
	valgrind --leak-check=full ./bin/jobExecutorServer

clean:
	rm -f ./bin/jobCommander ./bin/jobExecutorServer ./bin/progDelay

