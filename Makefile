#/bin/make

CC=gcc
OBJECTS=main.o fixedalloc.o
TARGET=test
#CFLAGS=-g -ggdb -O3 -Wall --std=gnu99
CFLAGS=-g -ggdb -Wall --std=gnu99 -DFIXEDALLOC_DEBUG

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) $(OBJECTS) $(TARGET)

