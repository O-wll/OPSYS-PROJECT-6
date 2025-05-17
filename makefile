GCC = gcc
CFLAGS = -g -Wall -Wshadow

# Make all objects and exe
all: oss worker

# Make exe 'oss'
oss: oss.o
	$(GCC) $(CFLAGS) oss.o  -o oss

# Make exe 'user'
worker: worker.o
	$(GCC) $(CFLAGS) worker.o -o worker

# Make oss object
oss.o: oss.c
	$(GCC) $(CFLAGS) -c -o oss.o oss.c

# Make user object
worker.o: worker.c
	$(GCC) $(CFLAGS) -c -o worker.o worker.c

# Clean object files and exe.
clean:
	rm -f worker.o oss.o oss worker
