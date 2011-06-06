#include "http-parser/http_parser.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/event.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>
#include <utility>

#define QLEN 32
#define BUFSIZE 4096
#define PORT_BASE 0

struct conn_state
{
	char *addr;
	http_parser *parser;
};	
	
/* used to store state for each connection,
   such as an http parser instance */
std::map<int, struct conn_state *> *connections = new std::map<int, struct conn_state *>();

/* request info */
unsigned int uri_len = 128;
char *uri_start;
char *uri = (char *)malloc(uri_len);

unsigned int query_len = 128;
char *query = (char *)malloc(query_len);

/* socket library error indicator */
extern int errno;

/* forwards for some support functions */
int errexit(const char *format, ...);
unsigned int passivesock(const char *service, const char *transport, int qlen);
void close_connection(int fd);

/* http parser callbacks */
int url_callback(http_parser *p, const char *at, size_t length);
int query_str_callback(http_parser *p, const char *at, size_t length);
int msg_complete_callback(http_parser *p);

int main(int argc, char *argv[])
{
	char buf[BUFSIZE];
	struct sockaddr_in client;
	int nevents, nparsed, kq, i;
	unsigned int len, servsock;
	struct conn_state *c;
	
	struct kevent ke;
	
	http_parser_settings parser_settings;
	
	/* set up http parser callbacks */
	parser_settings.on_url = url_callback;
	parser_settings.on_query_string = query_str_callback;
	parser_settings.on_message_complete = msg_complete_callback;
	
	/* listen socket */
	servsock = passivesock("http", "tcp", QLEN);
	
	/* create a new kernel event queue */
	kq = kqueue();
	if(kq == -1)
		errexit("kqueue() failed\n");
	
	/* initialize kevent structure */
	memset(&ke, 0, sizeof(struct kevent));
/* TODO: verbose description of line below */
	EV_SET(&ke, servsock, EVFILT_READ, EV_ADD, 0, 5, NULL);
	
	/* set the event */
	if(kevent(kq, &ke, 1, NULL, 0, NULL) == -1)
		errexit("kevent() failed on initial setup\n");
	
	while(1)
	{
		memset(&ke, 0, sizeof(ke));
		
		/* receive an event, a blocking call as timeout is NULL */
		nevents = kevent(kq, NULL, 0, &ke, 1, NULL);
		if(nevents == -1)
			errexit("kevent() failed in event loop\n");
		else if(nevents == 0)
			continue;
		else if(ke.ident == servsock)
		{
			/* server socket event, meaning a client is connecting */
			len = (socklen_t)sizeof(client);
			i = accept(servsock, (struct sockaddr *)&client, &len);
			if(i == -1)
				errexit("accept() failed: %s\n", strerror(errno));
			else
			{
				c = new struct conn_state;
				c->addr = strdup(inet_ntoa(client.sin_addr));
				if(c->addr == NULL)
					errexit("strdup() failed\n");
				c->parser = new http_parser;
				http_parser_init(c->parser, HTTP_REQUEST);
				c->parser->data = (void*)i;
				
				connections->insert(std::pair<int,struct conn_state *>(i, c));
				
				EV_SET(&ke, i, EVFILT_READ, EV_ADD, 0, 0, NULL);
				if(kevent(kq, &ke, 1, NULL, 0, NULL) == -1)
					errexit("kevent() failed when adding new client\n");
				
				printf("Connection from %s received\n", c->addr);
			}
		}
		else
		{
			/* a client has sent some data */
			memset(buf, 0, sizeof(buf));
			
			/* get conn_state object for client corresponding to event */
			c = (*connections)[(int)ke.ident];
			
			/* read from the client's socket */
			i = read((size_t)c->parser->data, buf, sizeof(buf));
			
			if(i == -1)
				continue;
			else if(i == 0)
			{
				/* EOF from client */
				printf("removing %s\n", c->addr);
				
				EV_SET(&ke, (uintptr_t)c->parser->data, EVFILT_READ, EV_DELETE, 0, 0, NULL);
				
				if(kevent(kq, &ke, 1, 0, 0, NULL) == -1)
					errexit("kevent() failed on removing a client\n");
				
				close_connection(ke.ident);
			}
			else
			{
				nparsed = http_parser_execute(c->parser, &parser_settings, buf, i);
				if(nparsed != i)
				{
					printf("error parsing request from client %s\n", c->addr);	
				}
			}
		}
	}
	
	return 0;
}

unsigned int passivesock(const char *service, const char *transport, int qlen)
{
	struct servent *pse;
	struct protoent *ppe;
	struct sockaddr_in sin;
	int s, type;
	
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	
	if((pse = getservbyname(service, transport)) != 0)
		sin.sin_port = htons(ntohs((unsigned short)pse->s_port) + PORT_BASE);
	else if((sin.sin_port = htons((unsigned short)atoi(service))) == 0)
		errexit("can't get \"%s\" service entry\n", service);
	
	if((ppe = getprotobyname(transport)) == 0)
		errexit("can't get \"%s\" protocol entry\n", transport);
	
	if(strcmp(transport, "udp") == 0)
		type = SOCK_DGRAM;
	else
		type = SOCK_STREAM;
	
	s = socket(PF_INET, type, ppe->p_proto);
	if(s < 0)
		errexit("can't create socket: %s\n", strerror(errno));
	
	if(bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		errexit("can't bind to %s port: %s\n", service, strerror(errno));
	if(type == SOCK_STREAM && listen(s, qlen) < 0)
		errexit("can't listen on %s port: %s\n", service, strerror(errno));
	
	return (unsigned int)s;
}

int errexit(const char *format, ...)
{
	va_list args;
	
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	exit(1);
}

void close_connection(int fd)
{
	close(fd);
	conn_state *c = (*connections)[fd];
	delete c;
	connections->erase(fd);
}

int url_callback(http_parser *p, const char *at, size_t length)
{
	uri_start = (char *)at;
	if(length-1 >= uri_len)
	{
		uri = (char *)realloc(uri, length);
		
		/* if realloc() returns NULL, there is no memory left... */
		if(uri == NULL)
			errexit("Ran out of memory (in url_callback)!\n");
		
		uri_len = length;
	}
	
	memcpy(uri, at+1, length-1);
	uri[length] = '\0';
	
	return 0;
}

int query_str_callback(http_parser *p, const char *at, size_t length)
{
	/* cut off the URI just before the query string */
	uri[at-uri_start-1] = '\0';
	
	if(length > 0)
	{
		if(length-1 >= query_len)
		{
			query = (char *)realloc(query, length);
		
			if(query == NULL)
				errexit("Ran out of memory (in query_str_callback)!\n");
		
			query_len = length;
		}

		memcpy(query, at+1, length-1);
	}
	
	query[length] = '\0';
	
	return 0;
}

int msg_complete_callback(http_parser *p)
{
	char *crlf = "\r\n";
	std::string status("HTTP/");
	char ver[5];
	sprintf(ver, "%1hi.%1hi ", p->http_major, p->http_minor);
	status += ver;
	
	struct iovec vectors[4];
	vectors[3].iov_base = crlf;
	vectors[3].iov_len = 2;
	
	/* send response */
	switch(p->method)
	{
		case HTTP_GET:
		case HTTP_HEAD:
		status += "200 OK\r\n";
		break;
		default:
		status += "501 Not Implemented\r\n";
	}
	
	/* status string iovector */
	vectors[0].iov_base = (void *)status.c_str();
	vectors[0].iov_len = status.length();
	
	/* response headers */
	vectors[1].iov_base = (void *)"Content-Type: text/html\r\nContent-Length: 14\r\n\r\n";
	vectors[1].iov_len = 47;

	/* not doing response body right now */
	vectors[2].iov_base = (void *)"Hello world!\r\n";
	vectors[2].iov_len = 14;
	
	writev((size_t)p->data, vectors, 3);
	
	close_connection((size_t)p->data);
	
	return 0;
}
