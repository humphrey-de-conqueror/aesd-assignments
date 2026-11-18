
# Default compiler prefix (empty for native compilation)
CROSS_COMPILE ?=

# Compiler
CC := $(CROSS_COMPILE)gcc

# Compiler flags
CFLAGS := -Wall -Werror -Wextra

# Target application
TARGET := writer

# Source and object files
SRCS := writer.c
OBJS := $(SRCS:.c=.o)

# Default target
all: $(TARGET)

# Link object files into executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

# Compile source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean generated files
clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
