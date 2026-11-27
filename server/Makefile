
# Native compilation by default.
# Cross-compilation example:
# make CROSS_COMPILE=aarch64-none-linux-gnu-

CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc

CFLAGS = -Wall -Wextra -Werror -std=gnu11 -g
LDFLAGS =

TARGET = aesdsocket
SRCS = aesdsocket.c
OBJS = $(SRCS:.c=.o)

.PHONY: all default clean

all: $(TARGET)

default: all

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)
