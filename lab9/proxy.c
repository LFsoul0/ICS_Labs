/*
 * proxy.c - ICS Web proxy
 *
 * Name: Wang Tingyu
 * ID: 519021910475
 */

#include "csapp.h"
#include <stdarg.h>
#include <sys/select.h>

/*
 * Global variable
 */
sem_t log_mutex;

/*
 * Struct for thread arguments
 */
struct arg_t {
    int connfd;
    struct sockaddr_in clientaddr;
};

/*
 * Function prototypes
 */
void* thread_routine(void* thread_arg);
void proxy_manager(int connfd, struct sockaddr_in* clientaddr);
int proxy_send(int clientfd, rio_t* conn_rio, char* req_header, size_t length, char* method);
size_t proxy_receive(int connfd, rio_t* client_rio);
int parse_uri(char *uri, char *target_addr, char *path, char *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);
ssize_t Rio_readnb_w(rio_t* rp, void* usrbuf, size_t n);
ssize_t Rio_readlineb_w(rio_t* rp, void* usrbuf, size_t maxlen);
int Rio_writen_w(int fd, void* usrbuf, size_t n);


/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    /* init vars */
    int listenfd = Open_listenfd(argv[1]);
    socklen_t clientlen = sizeof(struct sockaddr_in);
    pthread_t tid;;
    struct arg_t* thread_arg;

    /* init sem */
    Sem_init(&log_mutex, 0, 1);

    /* ignore SIGPIPE */
    Signal(SIGPIPE, SIG_IGN);

    /* listen to client */
    while (1) {
	thread_arg = Malloc(sizeof(struct arg_t));
	thread_arg->connfd = Accept(listenfd, (SA *)&(thread_arg->clientaddr), &clientlen);
	Pthread_create(&tid, NULL, &thread_routine, thread_arg);
    }

    /* never arrive here */
    Close(listenfd);
    exit(1);
}

/*
 * thread_routine - the main routine of every created thread
 */
void* thread_routine(void* thread_arg)
{
    /* set detach */
    Pthread_detach(Pthread_self());

    /* proxy manager */
    struct arg_t* arg_self = (struct arg_t*)thread_arg;
    proxy_manager(arg_self->connfd, &(arg_self->clientaddr));

    /* close and exit */
    Close(arg_self->connfd);
    Free(arg_self);
    return NULL;
}

/*
 * proxy_manager - manage proxy send and receive
 */
void proxy_manager(int connfd, struct sockaddr_in* clientaddr)
{
    char buf[MAXLINE], req_header[MAXLINE * 2];
    char method[MAXLINE / 4], uri[MAXLINE], version[MAXLINE / 2];
    char hostname[MAXLINE], pathname[MAXLINE], port[MAXLINE];
    int clientfd;
    rio_t conn_rio, client_rio;
    size_t byte_size = 0, content_length = 0;

    /* connect to server */
    Rio_readinitb(&conn_rio, connfd);
    if (!Rio_readlineb_w(&conn_rio, buf, MAXLINE)) {
	fprintf(stderr, "error: read empty request line\n");
	return;
    }
    if (sscanf(buf, "%s %s %s", method, uri, version) < 3) {
	fprintf(stderr, "error: mismatched parameters\n");
	return;
    }
    if (parse_uri(uri, hostname, pathname, port) != 0) {
	fprintf(stderr, "error: parse uri error\n");
	return;
    }

    /* set request header */
    sprintf(req_header, "%s /%s %s\r\n", method, pathname, version);
    size_t n = Rio_readlineb_w(&conn_rio, buf, MAXLINE);
    char tmp_header[MAXLINE]; 
    while(n != 0) {
	if (strncasecmp(buf, "Content-Length", 14) == 0) {
	    sscanf(buf + 15, "%zu", &content_length);
	}
        strcpy(tmp_header, req_header);
	sprintf(req_header, "%s%s", tmp_header, buf);

	if (strncmp(buf, "\r\n", 2) == 0) break;

	n = Rio_readlineb_w(&conn_rio, buf, MAXLINE);
    }
    /* if no content */
    if (n == 0) {
	return;
    }
    
    /* open clientfd */
    clientfd = open_clientfd(hostname, port);
    if (clientfd < 0) {
	fprintf(stderr, "error: open client fd error (hostname: %s, port: %s)\n", hostname, port);
	return;
    }
    Rio_readinitb(&client_rio, clientfd);

    /* send reqest to server */
    if (proxy_send(clientfd, &conn_rio, req_header, content_length, method) == 0) {
	byte_size = proxy_receive(connfd, &client_rio);
    }

    /* format log */
    format_log_entry(buf, clientaddr, uri, byte_size);
    P(&log_mutex);
    printf("%s\n", buf);
    V(&log_mutex);

    /* close */
    Close(clientfd);
}

/*
 * proxy_send - send request to server
 */
int proxy_send(int clientfd, rio_t* conn_rio, char* req_header, size_t length, char* method)
{
    char buf[MAXLINE];

    /* write request header */
    if (Rio_writen_w(clientfd, req_header, strlen(req_header)) == -1) {
	return -1;
    }

    /* write request body */
    if (strcasecmp(method, "GET") != 0) {
	for (int i = 0; i < length; ++i) {
	    if (Rio_readnb_w(conn_rio, buf, 1) == 0)
		return -1;
	    if (Rio_writen_w(clientfd, buf, 1) == -1)
		return -1;
	}
    }

    return 0;
}

/*
 * proxy_receive - receive response from server and send to connfd
 */
size_t proxy_receive(int connfd, rio_t* client_rio)
{
    char buf[MAXLINE];
    size_t byte_size = 0, content_length = 0;

    /* get response header */
    size_t n = Rio_readlineb_w(client_rio, buf, MAXLINE);
    while (n != 0) {
	byte_size += n;
	if (strncasecmp(buf, "Content-Length", 14) == 0) {
	    sscanf(buf + 15, "%zu", &content_length);
	}
	if (Rio_writen_w(connfd, buf, strlen(buf)) == -1) {
	    return 0;
	}

	if (strncmp(buf, "\r\n", 2) == 0) break;

	n = Rio_readlineb_w(client_rio, buf, MAXLINE);
    }
    /* if no content */
    if (n == 0) {
	return 0;
    }

    /* get response body */
    for (int i = 0; i < content_length; ++i) {
	if (Rio_readnb_w(client_rio, buf, 1) == 0)
	    return 0;
	if (Rio_writen_w(connfd, buf, 1) == -1) 
	    return 0;
	++byte_size;
    }

    return byte_size;
}

/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, char *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
        hostname[0] = '\0';
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    if (hostend == NULL)
        return -1;
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    if (*hostend == ':') {
        char *p = hostend + 1;
        while (isdigit(*p))
            *port++ = *p++;
        *port = '\0';
    } else {
        strcpy(port, "80");
    }

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
        pathname[0] = '\0';
    }
    else {
        pathbegin++;
        strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), the number of bytes
 * from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
                      char *uri, size_t size)
{
    time_t now;
    char time_str[MAXLINE];
    char host[INET_ADDRSTRLEN];

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    if (inet_ntop(AF_INET, &sockaddr->sin_addr, host, sizeof(host)) == NULL)
        unix_error("Convert sockaddr_in to string representation failed\n");

    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %s %s %zu", time_str, host, uri, size);
}

/* 
 * Rio error checking wrappers
 */
ssize_t Rio_readnb_w(rio_t* rp, void* usrbuf, size_t n)
{
    ssize_t rc;

    if ((rc = rio_readnb(rp, usrbuf, n)) < 0) {
	fprintf(stderr, "Rio_readnb error\n");
	return 0;
    }
    return rc;
}

ssize_t Rio_readlineb_w(rio_t* rp, void* usrbuf, size_t maxlen)
{
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
	fprintf(stderr, "Rio_readlineb error\n");
	return 0;
    }
    return rc;
}

int Rio_writen_w(int fd, void* usrbuf, size_t n)
{
    if (rio_writen(fd, usrbuf, n) != n) {
	fprintf(stderr, "Rio_writen error\n");
	return -1;
    }
    return 0;
}
