# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall

# Source files
SRCS = ppos-core-aux.c ppos-core-ipc.c signal-test.c

# Object files
OBJS = queue.o ppos-all.o

# Output executable
TARGET = ppos

# Default rule
all: clean $(TARGET)

# Linking
$(TARGET): $(SRCS) $(OBJS)
	$(CC) $(CFLAGS) $(SRCS) $(OBJS) -o $(TARGET)

# Clean rule
clean:
	rm -f $(TARGET)
