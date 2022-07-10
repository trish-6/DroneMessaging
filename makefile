# Makefile for Lab 10 program
# Tristan Langley.67

CC = gcc
CFLAGS += -g -Wall -lm

drone10: drone10.c
	$(CC) $(CFLAGS) -o drone10 drone10.c

clean:
	rm -f drone10 *.gc*

