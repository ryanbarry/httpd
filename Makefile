CPPFLAGS=-Wall -Wextra -I http-parser
OPT_DEBUG=$(CPPFLAGS) -O0 -g -DHTTP_PARSER_STRICT=1
OPT_FAST=$(CPPFLAGS) -O3 -DHTTP_PARSER_STRICT=0
TARGET=httpd
CC=g++

httpd: main.cpp http-parser/http_parser.c http-parser/http_parser.h
	$(CC) $(CPPFLAGS) main.cpp http-parser/http_parser.c -o $@

clean:
	rm $(TARGET)
