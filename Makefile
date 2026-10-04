CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude

TARGET = ptymux

SRC = src/main.c \
      src/pty.c \
      src/terminal.c \
      src/session.c \
      src/multiplexer.c

OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

run: $(TARGET)
	./$(TARGET)