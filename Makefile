TARGET=httpd
CC=g++

$(TARGET): http-parser/http_parser.o main.cpp
	$(CC) -Wall -c main.cpp -o main.o
	$(CC) -o $(TARGET) http-parser/http_parser.o main.o

http-parser/http_parser.o:
	$(MAKE) --directory=http-parser http_parser.o

clean:
	rm -f $(TARGET) main.o
	$(MAKE) --directory=http-parser clean

.PHONY: clean
