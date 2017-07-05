CC = gcc
CFLAGS = -pthread -Wall -Wimplicit-function-declaration
CLIBS = 
TARGET = mctest
CSOURCE = main.c

all : $(TARGET)

$(TARGET) : $(CSOURCE)
	$(CC) $(CFLAGS) $(CLIBS) -o $@ $<

clean:
	rm -f $(TARGET)
