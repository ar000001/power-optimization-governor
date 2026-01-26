CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lm

TARGET = governor
SRCS = main.c Governor.c PipelineConfig.c ApproximationModels.c PIDController.c
OBJS = $(SRCS:.c=.o)
HEADERS = Governor.h PipelineConfig.h ApproximationModels.h PIDController.h

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
