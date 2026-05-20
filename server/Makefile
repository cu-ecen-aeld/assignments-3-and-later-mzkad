CC ?= gcc

C_FLAGS ?= -Wall -Wextra -Werror

LDFLAGS ?= -lpthread

TARGET = aesdsocket

SRC = aesdsocket.c

default: $(TARGET)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) *.o
