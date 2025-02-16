# Compiler
CC = gcc

# Compiler flags
CFLAGS =

# Source files
COMMON_SRCS = ppos-core-aux.c
MQUEUE_SRCS = pingpong-mqueue.c
RACECOND_SRCS = pingpong-racecond.c
SEMAPHORE_SRCS = pingpong-semaphore.c
DISK1_SRCS = disk-driver.c ppos-disk-manager.c pingpong-disco1.c
DISK2_SRCS = disk-driver.c ppos-disk-manager.c pingpong-disco2.c

# Object files
OBJS = queue.o ppos-all.o

# Output executables
MQUEUE_TARGET = mqueue
RACECOND_TARGET = racecond
SEMAPHORE_TARGET = semaphore
PPOS_DISCO1_TARGET = ppos-disco1
PPOS_DISCO2_TARGET = ppos-disco2

LIBS = -lm -lrt

# Default rule
all: clean $(MQUEUE_TARGET) $(RACECOND_TARGET) $(SEMAPHORE_TARGET) $(PPOS_DISCO1_TARGET) $(PPOS_DISCO2_TARGET)

# Linking for mqueue
$(MQUEUE_TARGET): $(COMMON_SRCS) $(MQUEUE_SRCS) $(OBJS)
	$(CC) $(CFLAGS) $(COMMON_SRCS) $(MQUEUE_SRCS) $(OBJS) -o $(MQUEUE_TARGET) $(LIBS)

# Linking for racecond
$(RACECOND_TARGET): $(COMMON_SRCS) $(RACECOND_SRCS) $(OBJS)
	$(CC) $(CFLAGS) $(COMMON_SRCS) $(RACECOND_SRCS) $(OBJS) -o $(RACECOND_TARGET) $(LIBS)

# Linking for semaphore
$(SEMAPHORE_TARGET): $(COMMON_SRCS) $(SEMAPHORE_SRCS) $(OBJS)
	$(CC) $(CFLAGS) $(COMMON_SRCS) $(SEMAPHORE_SRCS) $(OBJS) -o $(SEMAPHORE_TARGET) $(LIBS)

# Linking for ppos-disco1
$(PPOS_DISCO1_TARGET): $(COMMON_SRCS) $(DISK1_SRCS) $(OBJS)
	$(CC) $(CFLAGS) $(COMMON_SRCS) $(DISK1_SRCS) $(OBJS) -o $(PPOS_DISCO1_TARGET) $(LIBS)

# Linking for ppos-disco2
$(PPOS_DISCO2_TARGET): $(COMMON_SRCS) $(DISK2_SRCS) $(OBJS)
	$(CC) $(CFLAGS) $(COMMON_SRCS) $(DISK2_SRCS) $(OBJS) -o $(PPOS_DISCO2_TARGET) $(LIBS)

# Clean rule
clean:
	rm -f $(MQUEUE_TARGET) $(RACECOND_TARGET) $(SEMAPHORE_TARGET) $(PPOS_DISCO1_TARGET) $(PPOS_DISCO2_TARGET)