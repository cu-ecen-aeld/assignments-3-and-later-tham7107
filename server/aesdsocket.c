//////////////////////////////////////////////////////////////////////
//
// Thomas Ames
// ECEA 5305, assignment #5, socket.c
// July 2023
//

//#include <sys/stat.h>
//#include <fcntl.h>
//#include <errno.h>
//#include <string.h>

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

#define TCP_PORT "9000"
#define SOCKET_LISTEN_BACKLOG 5
#define IP_ADDR_MAX_STRLEN 20

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

int main(int argc, char *argv[])
{
    int sock_fd, conn_fd;
    fd_set fds;
    struct addrinfo getaddrinfo_hints;
    struct addrinfo *server_addr;
    struct sockaddr client_addr;
    socklen_t client_addr_len;
    char client_ip_addr_str[IP_ADDR_MAX_STRLEN];
    char client_port_str[IP_ADDR_MAX_STRLEN];

    // Connect SIGINT and SIGTERM
    if (setup_signals()) {
	exit(EXIT_FAILURE);
    }

    // socket, bind, listen, accept
    // Class example uses PF_*, man page says AF_* is the standard
    // AF_INET = IPv4, AF_INET6 = IPv6
    if (-1 == (sock_fd = socket(AF_INET, SOCK_STREAM, 0))) {
	perror("socket");
	exit(EXIT_FAILURE);
    }

    bzero(&getaddrinfo_hints, sizeof(struct addrinfo));
    getaddrinfo_hints.ai_family   = AF_UNSPEC;   // IPv4 or v6
    getaddrinfo_hints.ai_socktype = SOCK_STREAM; // TCP
    getaddrinfo_hints.ai_flags    = AI_PASSIVE;
    if (getaddrinfo(NULL, TCP_PORT, &getaddrinfo_hints, &server_addr)) {
	perror("getaddrinfo");
	exit(EXIT_FAILURE);
    }
    if (bind(sock_fd, server_addr->ai_addr, sizeof(struct sockaddr))) {
	perror("bind");
	exit(EXIT_FAILURE);
    }
    freeaddrinfo(server_addr);
    if (listen(sock_fd, SOCKET_LISTEN_BACKLOG)) {
	perror("listen");
	exit(EXIT_FAILURE);
    }
    client_addr_len = sizeof(struct sockaddr);
    if (-1 == (conn_fd = accept(sock_fd, &client_addr, &client_addr_len))) {
	perror("accept");
	exit(EXIT_FAILURE);
    }
    if (getnameinfo(&client_addr, sizeof(client_addr),
		    client_ip_addr_str, IP_ADDR_MAX_STRLEN,
		    client_port_str, IP_ADDR_MAX_STRLEN,
		    NI_NUMERICHOST | NI_NUMERICSERV)) {
	perror("getnameinfo");
	exit(EXIT_FAILURE);
    }
    syslog(LOG_USER|LOG_INFO, "Accepted connection from %s",
           client_ip_addr_str);
    //syslog(LOG_USER|LOG_INFO, "Accepted connection from %s:%s",
    //       client_ip_addr_str, client_port_str);

    printf("blah\n");
    FD_ZERO(&fds);
    FD_SET(0,&fds);
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
}
