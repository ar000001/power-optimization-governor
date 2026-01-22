CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS =

TARGET = governor
SRCS = main.c Governor.c PipelineConfig.c
OBJS = $(SRCS:.c=.o)
HEADERS = Governor.h PipelineConfig.h

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
