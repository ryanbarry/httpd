#include "http-parser/http_parser.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/event.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <map>
#include <utility>

#define QLEN 32
#define BUFSIZE 4096
#define PORT_BASE 8000

struct conn_state
{
	char *addr;
	http_parser *parser;
};

/* socket library error indicator */
extern int errno;

/* forwards for some support functions */
int errexit(const char *format, ...);
int passivesock(const char *service, const char *transport, int qlen);

/* http parser callbacks */
int url_callback(http_parser *p, const char *at, size_t length);

int main(int argc, char *argv[])
{
	char *service = "http";
	char buf[BUFSIZE];
	struct sockaddr_in client;
	int servsock, nevents, nparsed, kq, i;
	unsigned int len;
	struct conn_state *c;
	
	struct kevent ke;
	
	/* used to store state for each connection,
	   such as an http parser instance */
	std::map<int, struct conn_state *> *connections = new std::map<int, struct conn_state *>();
	
	http_parser_settings parser_settings;
	
	switch(argc) {
		case 1:
		break;
		
		case 2:
			service = argv[1];
		break;
		
		default:
			errexit("usage: httpd [port]\n");
	}
	
	/* set up http parser callbacks */
	parser_settings.on_url = url_callback;
	
	/* listen socket */
	servsock = passivesock(service, "tcp", QLEN);
	
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
				
				close((size_t)c->parser->data);
				delete c;
				connections->erase(ke.ident);
			}
			else
			{
				printf("received following request from client %s:\n%s", c->addr, buf);
				nparsed = http_parser_execute(c->parser, &parser_settings, buf, i);
				if(nparsed != i)
				{
					printf("error parsing request from client %s; (should close connection)\n", c->addr);	
				}
			}
		}
	}
	
	return 0;
}

int passivesock(const char *service, const char *transport, int qlen)
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
	
	return s;
}

int errexit(const char *format, ...)
{
	va_list args;
	
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	exit(1);
}

int url_callback(http_parser *p, const char *at, size_t length)
{
	printf("url_callback() - len: %d; at: %s\n", (uint)length, at);
}
