# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall

# Source files
COMMON_SRCS = ppos-core-aux.c ppos-ipc.c
MQUEUE_SRCS = pingpong-mqueue.c
RACECOND_SRCS = pingpong-racecond.c
SEMAPHORE_SRCS = pingpong-semaphore.c

# Object files
OBJS = queue.o ppos-all.o

# Output executables
MQUEUE_TARGET = ppos-mqueue
RACECOND_TARGET = ppos-racecond
SEMAPHORE_TARGET = ppos-semaphore

LIBS = -lm

# Default rule
all: clean $(MQUEUE_TARGET) $(RACECOND_TARGET) $(SEMAPHORE_TARGET)

# Linking for mqueue
$(MQUEUE_TARGET): $(COMMON_SRCS) $(MQUEUE_SRCS) $(OBJS)
	$(CC) $(CFLAGS) $(COMMON_SRCS) $(MQUEUE_SRCS) $(OBJS) -o $(MQUEUE_TARGET) $(LIBS)

# Linking for racecond
$(RACECOND_TARGET): $(COMMON_SRCS) $(RACECOND_SRCS) $(OBJS)
	$(CC) $(CFLAGS) $(COMMON_SRCS) $(RACECOND_SRCS) $(OBJS) -o $(RACECOND_TARGET) $(LIBS)

# Linking for semaphore
$(SEMAPHORE_TARGET): $(COMMON_SRCS) $(SEMAPHORE_SRCS) $(OBJS)
	$(CC) $(CFLAGS) $(COMMON_SRCS) $(SEMAPHORE_SRCS) $(OBJS) -o $(SEMAPHORE_TARGET) $(LIBS)

# Clean rule
clean:
	rm -f $(MQUEUE_TARGET) $(RACECOND_TARGET) $(SEMAPHORE_TARGET)
