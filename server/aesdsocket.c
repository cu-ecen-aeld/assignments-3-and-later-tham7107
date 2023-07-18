//////////////////////////////////////////////////////////////////////
//
// Thomas Ames
// ECEA 5305, assignment #5, socket.c
// July 2023
//

//#include <sys/stat.h>
//#include <fcntl.h>

#include <stdio.h>
#include <stddef.h>
#include <signal.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>

#define TCP_PORT "9000"
#define SOCKET_LISTEN_BACKLOG 5
#define IP_ADDR_MAX_STRLEN 20
#define SOCK_READ_BUF_SIZE 100

int caught_signal = 0;

// Simple signal handler; sets global flag to non-zero on caught signal
void signal_handler(int signum)
{
    caught_signal = signum;
}

// Connect signal_handler(int signum) to SIGINT and SIGTERM
// Returns 0 on success, non-zero on error
int setup_signals()
{
    struct sigaction new_sigaction;

    bzero(&new_sigaction, sizeof(struct sigaction));
    new_sigaction.sa_handler = signal_handler;
    if (sigaction(SIGINT, &new_sigaction, NULL)) {
	perror("sigaction SIGINT");
	return(1);
    }
    if (sigaction(SIGTERM, &new_sigaction, NULL)) {
	perror("sigaction SIGTERM");
	return(1);
    }
    return(0);
}

// Handle initial portions of socket setup - socket, bind, listen calls.
// Returns a socket fd that can be passed to accept on success, -1 on failure
int socket_init()
{
    int sock_fd;
    struct addrinfo getaddrinfo_hints;
    struct addrinfo *server_addr;

    // socket, bind, listen, accept
    // Class example uses PF_*, man page says AF_* is the standard
    // AF_INET = IPv4, AF_INET6 = IPv6
    if (-1 == (sock_fd = socket(AF_INET, SOCK_STREAM, 0))) {
	perror("socket");
	return(-1);
    }

    // Init the hints struct
    bzero(&getaddrinfo_hints, sizeof(struct addrinfo));
    getaddrinfo_hints.ai_family   = AF_UNSPEC;   // IPv4 or v6
    getaddrinfo_hints.ai_socktype = SOCK_STREAM; // TCP
    getaddrinfo_hints.ai_flags    = AI_PASSIVE;

    // Use getaddrinfo to get the bind address in server_addr.  Must
    // free it with freeaddrinfo(server_addr) to avoid memory leaks
    if (getaddrinfo(NULL, TCP_PORT, &getaddrinfo_hints, &server_addr)) {
	perror("getaddrinfo");
	return(-1);
    }

    // Technically, getaddrinfo returns a linked list of addrs in
    // server_addt, but we are just using the first one.  See
    // the Beej docs, https://beej.us/guide/bgnet/html/#a-simple-stream-client
    if (bind(sock_fd, server_addr->ai_addr, sizeof(struct sockaddr))) {
	perror("bind");
	return(-1);
    }

    freeaddrinfo(server_addr);

    if (listen(sock_fd, SOCKET_LISTEN_BACKLOG)) {
	perror("listen");
	return(-1);
    }
    return sock_fd;
}

int wait_for_client_connection()
{
    // Wait for client connections.  accept can be interrupted with a
    // caught SIGINT or SIGTERM; log message if so.
    client_addr_len = sizeof(struct sockaddr);
    if (-1 == (conn_fd = accept(sock_fd, &client_addr, &client_addr_len))) {
	if (EINTR == errno) {
	    // System call, log message and exit cleanly
	    syslog(LOG_USER|LOG_INFO,"Caught signal, exiting");
	    ret_val = EXIT_SUCCESS;
	    break;
	} else {
	    perror("accept");
	    ret_val = EXIT_FAILURE;
	    break;
	}
    } else {
	// conn_fd == new connection from client.
	// Use getnameinfo to parse client IP address and port number
	if (getnameinfo(&client_addr, sizeof(client_addr),
			client_ip_addr_str, IP_ADDR_MAX_STRLEN,
			client_port_str, IP_ADDR_MAX_STRLEN,
			NI_NUMERICHOST | NI_NUMERICSERV)) {
	    perror("getnameinfo");
	    ret_val = EXIT_FAILURE;
	    break;
	}
	syslog(LOG_USER|LOG_INFO, "Accepted connection from %s",
	       client_ip_addr_str);
	//syslog(LOG_USER|LOG_INFO, "Accepted connection from %s:%s",
	//       client_ip_addr_str, client_port_str);
    }
}

int main(int argc, char *argv[])
{
    int sock_fd, conn_fd;
    fd_set fds;
    struct sockaddr client_addr;
    socklen_t client_addr_len;
    char client_ip_addr_str[IP_ADDR_MAX_STRLEN];
    char client_port_str[IP_ADDR_MAX_STRLEN];
    int done, ret_val;
    char *read_buf;

    // Connect SIGINT and SIGTERM
    if (setup_signals()) {
	exit(EXIT_FAILURE);
    }

    // Set up the socket with socket, bind, listen calls
    if (-1 == (sock_fd = socket_init())) {
	// perror's inside socket_init()
	exit(EXIT_FAILURE);
    }

    done = 0;
    while (!done) {
	    if (!(read_buf = malloc(SOCK_READ_BUF_SIZE))) {
		perror("malloc");
		ret_val = EXIT_FAILURE;
		break;
	    }
	    // Number of bytes read is length of string (with
	    // newline), but no null terminator is added.
      	    {int bc; bc=read(conn_fd,read_buf,SOCK_READ_BUF_SIZE);
	    printf("%d,%d,%s",bc,(int)strlen(read_buf),read_buf);}
	}
    }
    printf("blah\n");
    FD_ZERO(&fds);
    FD_SET(0,&fds);
#if 0
    struct timeval timeout;
    int rc;
    while(1) {
//	pause();
	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
	// Select returns w/ -1 if a signal is caught or on error
	rc=select(1, &fds, NULL, NULL, &timeout);
	if (caught_signal) {
	    syslog(LOG_USER|LOG_INFO,"Caught signal, exiting");
	    exit(EXIT_SUCCESS);
	}
	printf("select return, rc=%d\n",rc);
	printf("Caught signal #%d\n",caught_signal);
	printf("SIGTERM=#%d\n",SIGTERM);
	printf("SIGINT=#%d\n",SIGINT);
    }

    syslog(LOG_USER|LOG_INFO, "Closed connection from %s",
           client_ip_addr_str);
    //syslog(LOG_USER|LOG_INFO, "Closed connection from %s:%s",
    //       client_ip_addr_str, client_port_str);
    exit(EXIT_SUCCESS);
#endif
    return(ret_val);
}
