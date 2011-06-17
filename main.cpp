#include "http-parser/http_parser.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
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
#include <ctime>
#include <fstream>
#include <map>
#include <string>
#include <utility>

#define QLEN 32
#define BUFSIZE 4096
#define PORT_BASE 0

struct conn_state
{
	int sockfd;
	char *addr;
	http_parser *parser;
	std::map<std::string, std::string> req_headers;
};	
	
/* used to store state for each connection,
   such as an http parser instance */
std::map<int, conn_state *> *connections = new std::map<int, conn_state *>();

/* request info */
std::string path;
std::string query;
std::string url;
std::string header_field;

/* socket library error indicator */
extern int errno;

/* global kqueue */
int kq;
struct kevent ke;

/* forwards for some support functions */
int errexit(const char *format, ...);
unsigned int passivesock(const char *service, const char *transport, int qlen);
void close_connection(int fd);
void append_date_header(std::string *response_head);

/* http parser callbacks */
int msg_begin_callback(http_parser *p);
int path_callback(http_parser *p, const char *at, size_t length);
int query_str_callback(http_parser *p, const char *at, size_t length);
int url_callback(http_parser *p, const char *at, size_t length);
int header_field_callback(http_parser *p, const char *at, size_t length);
int header_value_callback(http_parser *p, const char *at, size_t length);
int msg_complete_callback(http_parser *p);

/* http response methods */
bool http_respond_get(conn_state *cs);
bool http_respond_head(conn_state *cs);
bool http_respond_default(conn_state *cs);

int main(int argc, char *argv[])
{
	char buf[BUFSIZE];
	struct sockaddr_in client;
	int nevents, i;
	unsigned int len, servsock, nbytes;
	conn_state *c;
	
	
	http_parser_settings parser_settings;
	
	/* set up http parser callbacks */
	parser_settings.on_message_begin = msg_begin_callback;
	parser_settings.on_path = path_callback;
	parser_settings.on_query_string = query_str_callback;
	parser_settings.on_url = url_callback;
	parser_settings.on_header_field = header_field_callback;
	parser_settings.on_header_value = header_value_callback;
	parser_settings.on_message_complete = msg_complete_callback;
	
	/* listen socket */
	servsock = passivesock("http", "tcp", QLEN);
	
	/* create a new kernel event queue */
	kq = kqueue();
	if(kq == -1)
		errexit("kqueue() failed\n");
	
	/* initialize kevent structure */
	memset(&ke, 0, sizeof(struct kevent));
	/* ke is the kevent struct
	   servsock is the file descriptor of the socket
	   EVFILT_READ is the filter type
	   EV_ADD means we're adding this event to the queue
	   0 means there are no filter-specific flags
	   0 means there is no data to go with this filter
	   NULL means there is no timeout, this call will block */
	EV_SET(&ke, servsock, EVFILT_READ, EV_ADD, 0, 0, NULL);
	
	/* set the event */
	if(kevent(kq, &ke, 1, NULL, 0, NULL) == -1)
		errexit("kevent() failed on initial setup\n");
	
	while(1)
	/* event loop */
	{
		/* receive an event, a blocking call as timeout is NULL */
		nevents = kevent(kq, NULL, 0, &ke, 1, NULL);
		if(nevents > 0)
		{
			if(ke.ident == servsock)
			/* server socket event, meaning a client is connecting */
			{
				len = (socklen_t)sizeof(client);
				i = accept(servsock, (struct sockaddr *)&client, &len);

				if(i == -1)
					errexit("accept() failed: %s\n", strerror(errno));
				else
				{
					c = new conn_state;
					c->sockfd = i;
					c->addr = strdup(inet_ntoa(client.sin_addr));
					if(c->addr == NULL)
						errexit("strdup() failed\n");
					c->parser = new http_parser;
					http_parser_init(c->parser, HTTP_REQUEST);
					c->parser->data = (void*)i;
					
					connections->insert(std::pair<int, conn_state *>(i, c));

					EV_SET(&ke, i, EVFILT_READ, EV_ADD, 0, 0, NULL);
					if(kevent(kq, &ke, 1, NULL, 0, NULL) == -1)
						errexit("kevent() failed when adding new client\n");
				}
			}
			else
			/* a client has sent some data */
			{
				//memset(buf, 0, sizeof(buf));
				
				/* get conn_state object for client corresponding to event */
				c = (*connections)[(int)ke.ident];
				
				/* read from the client's socket */
				nbytes = read(c->sockfd, buf, sizeof(buf));
				
				if(nbytes > 0)
				{
					if(http_parser_execute(c->parser, &parser_settings, buf, nbytes) != nbytes)
					{
						printf("error parsing request from client %s (on byte %d/%d)\n", c->addr, i, nbytes);
						close_connection(ke.ident);
					}
				}
				else if(nbytes == 0)
				{
					/* EOF from client */
					close_connection(ke.ident);
				}
				else
				{
					printf("Read error: %s\n", strerror(errno));
					close_connection(ke.ident);
				}
			}
		}	
		else if(nevents == 0)
			continue;
		else /* nevents == -1 */
			errexit("kevent() failed in event loop\n");
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
/*	events are automatically deleted on "the last close of the descriptor"
	EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	
	if(kevent(kq, &ke, 1, 0, 0, NULL) == -1)
		errexit("kevent() failed on removing a client\n");
*/
	close(fd);
	conn_state *c = (*connections)[fd];
	delete c;
	connections->erase(fd);
}

void append_date_header(std::string *response_head)
{
	tm *ptm;
	time_t now = time(NULL);
	ptm = gmtime(&now);
	char tmp[38];
	
	/* date string example: Sun, 06 Nov 1994 08:49:37 GMT */
	strftime(tmp, 37, "Date: %a, %d %b %Y %H:%M:%S GMT\r\n", ptm);
	
	response_head->append(tmp);
}

int msg_begin_callback(http_parser *p)
{
	conn_state *cs = (*connections)[(size_t)p->data];
		
	/* clear all the headers stored for this connection (if this isn't a new connection) */
	if(!cs->req_headers.empty())
		cs->req_headers.clear();
	
	return 0;
}

int path_callback(http_parser *p, const char *at, size_t length)
{
	unsigned int i, ptr = 0;
	char ascii, a, b;
	
	// empty previously set path string
	path.clear();
	for(i = 0; i < length; i++)
	{
		if(at[i] == '%')
		{
			// append the stuff up to this point
			path.append(at+ptr, i-ptr);
			
			// convert hex digits to char
			a = at[i+1], b = at[i+2];
			if(a >=97)
		    {
				ascii = (a - 97) + 10;
		    }
		    else if(a >= 65)
		    {
				ascii = (a - 65) + 10;
		    }
		    else
		    {
				ascii = a - 48;
		    }
			ascii <<= 4;
			if(b >=97)
		    {
				ascii += (b - 97) + 10;
		    }
		    else if(a >= 65)
		    {
				ascii += (b - 65) + 10;
		    }
		    else
		    {
				ascii += b - 48;
		    }
			
			// append character to string
			path.append(&ascii, 1);
			
			// skip ahead
			i += 2;
			ptr = i+1;
		}
	}
	// append remaining stuff
	path.append(at+ptr, i-ptr);
	
	return 0;
}

int query_str_callback(http_parser *p, const char *at, size_t length)
{
	query.assign(at, length);
	
	return 0;
}

int url_callback(http_parser *p, const char *at, size_t length)
{
	url.assign(at, length);
	
	return 0;
}

int header_field_callback(http_parser *p, const char *at, size_t length)
{
	header_field.assign(at, length);
	
	return 0;
}

int header_value_callback(http_parser *p, const char *at, size_t length)
{
	conn_state *cs = (*connections)[(size_t)p->data];
	
	std::string header_val(at, length);
	
	cs->req_headers[header_field] = header_val;
	
	return 0;
}

int msg_complete_callback(http_parser *p)
{
	bool keepalive;
	conn_state *cs = (*connections)[(size_t)p->data];
	
	/* send response */
	switch(p->method)
	{
		case HTTP_GET:
		keepalive = http_respond_get(cs);
		break;
		case HTTP_HEAD:
		keepalive = http_respond_head(cs);
		break;
		default:
		keepalive = http_respond_default(cs);
	}
	
	if(!keepalive)
		close_connection((size_t)p->data);
	
	return 0;
}

bool http_respond_get(conn_state *cs)
{
	struct iovec vectors[2];
	struct stat info;
	char tmp[32];
	bool keepalive;
	std::string response_head;
	
	if(stat(path.c_str()+1, &info) != 0)
	{
		std::string response_body("<!DOCTYPE html>\n<html lang=en><head><title>Error 404 (Not Found)</title></head>"
		"<body><h1>HTTP Client Error 404</h1><p>The resource requested could not be found.</p></body></html>\n");
		
		response_head.append("HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n");
		
		append_date_header(&response_head);
		
		sprintf(tmp, "Content-Length: %d\r\n", (int)response_body.length());
		response_head.append(tmp);
		
		if(cs->req_headers["Connection"] == "close")
		{
			keepalive = false;
			response_head.append("Connection: close\r\n\r\n");
		}
		else
		{
			keepalive = true;
			response_head.append("\r\n");
		}		

		vectors[0].iov_base = (void *)response_head.c_str();
		vectors[0].iov_len = response_head.length();
		
		vectors[1].iov_base = (void *)response_body.c_str();
		vectors[1].iov_len = response_body.length();
		
		writev(cs->sockfd, vectors, 2);
	}
	else
	{
		std::ifstream ifs;
		ifs.open(path.c_str()+1, std::ios::binary);
		char *filebuf = new char[info.st_size];
		ifs.read(filebuf, info.st_size);
		ifs.close();
		
		response_head.append("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n");
		append_date_header(&response_head);
		sprintf(tmp, "Content-Length: %d\r\n", (int)info.st_size);
		response_head.append(tmp);
		
		if(cs->req_headers["Connection"] == "close")
		{
			keepalive = false;
			response_head.append("Connection: close\r\n\r\n");
		}
		else
		{
			keepalive = true;
			response_head.append("\r\n");
		}
		
		vectors[0].iov_base = (void *)response_head.c_str();
		vectors[0].iov_len = response_head.length();
		
		vectors[1].iov_base = filebuf;
		vectors[1].iov_len = info.st_size;
		
		writev(cs->sockfd, vectors, 2);
		
		delete[] filebuf;
	}
	
	return keepalive;
}

bool http_respond_head(conn_state *cs)
{
	struct iovec vector[1];
	struct stat info;
	char tmp[32];
	bool keepalive;
	std::string response_head;
	
	if(stat(path.c_str()+1, &info) != 0)
	{
		response_head.append("HTTP/1.1 404 Not Found\r\n");
		
		append_date_header(&response_head);
		
		if(cs->req_headers["Connection"] == "close")
		{
			keepalive = false;
			response_head.append("Connection: close\r\n\r\n");
		}
		else
		{
			keepalive = true;
			response_head.append("\r\n");
		}		

		vector[0].iov_base = (void *)response_head.c_str();
		vector[0].iov_len = response_head.length();
		
		writev(cs->sockfd, vector, 1);
	}
	else
	{
		response_head.append("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n");
		append_date_header(&response_head);
		sprintf(tmp, "Content-Length: %d\r\n", (int)info.st_size);
		response_head.append(tmp);
		
		if(cs->req_headers["Connection"] == "close")
		{
			keepalive = false;
			response_head.append("Connection: close\r\n\r\n");
		}
		else
		{
			keepalive = true;
			response_head.append("\r\n");
		}
		
		vector[0].iov_base = (void *)response_head.c_str();
		vector[0].iov_len = response_head.length();
		
		writev(cs->sockfd, vector, 1);
	}
	
	return keepalive;
}

bool http_respond_default(conn_state *cs)
{
	struct iovec vectors[2];
	char tmp[20];
	std::string response_body("<!DOCTYPE html>\n<html lang=en><head><title>Error 501 (Not Implemented)</title></head>"
	"<body><h1>HTTP Server Error 501</h1><p>The server encountered an error processing your request."
	"The method your client specified is not implemented by this server.</p></body></html>");
	std::string response_head("HTTP/1.1 501 Not Implemented\r\nContent-Type: text/html\r\n");
	sprintf(tmp, "Content-Length: %d\r\n", (int)response_body.length());
	response_head.append(tmp);
	append_date_header(&response_head);
	response_head.append("Connection: close\r\n");
	
	vectors[0].iov_base = (void *)response_head.c_str();
	vectors[0].iov_len = response_head.length();
	
	vectors[1].iov_base = (void *)response_body.c_str();
	vectors[1].iov_len = response_body.length();
	
	writev(cs->sockfd, vectors, 2);
	
	return false;
}
