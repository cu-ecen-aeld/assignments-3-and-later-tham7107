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

#define DEBUG 1

#define TCP_PORT "9000"
#define SOCKET_LISTEN_BACKLOG 5
#define IP_ADDR_MAX_STRLEN 20
#define SOCK_READ_BUF_SIZE 100
#define DATAFILE "/var/tmp/aesdsocketdata"

#ifdef DEBUG
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

int caught_signal = 0;

// Simple signal handler; sets global flag to non-zero on caught signal
// Realistically, this will never get called, as we will probably only
// see a ctrl-C while waiting in accept or recv.  In those cases, since
// we have installed this signal handler, the system call will be
// interrupted and return -1 with errno==EINTR, but signal handlers
// are not called while inside a system call.
void signal_handler(int signum)
{
    caught_signal = signum;
}

// Connect signal_handler(int signum) to SIGINT and SIGTERM
// Returns on success, exits on error.
void setup_signals()
{
    struct sigaction new_sigaction;

    bzero(&new_sigaction, sizeof(struct sigaction));
    new_sigaction.sa_handler = signal_handler;
    if (sigaction(SIGINT, &new_sigaction, NULL)) {
	perror("sigaction SIGINT");
	exit(EXIT_FAILURE);
    }
    if (sigaction(SIGTERM, &new_sigaction, NULL)) {
	perror("sigaction SIGTERM");
	exit(EXIT_FAILURE);
    }
}

// Handle initial portions of socket setup - socket, bind, listen calls.
// Returns a socket fd on success that can be passed to accept on success,
// exits on error.
int socket_init()
{
    int sock_fd, sock_opts;
    struct addrinfo getaddrinfo_hints;
    struct addrinfo *server_addr;

    // socket, bind, listen, accept
    // Class example uses PF_*, man page says AF_* is the standard
    // AF_INET = IPv4, AF_INET6 = IPv6
    if (-1 == (sock_fd = socket(AF_INET, SOCK_STREAM, 0))) {
	perror("socket");
	exit(EXIT_FAILURE);
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
	close(sock_fd);
	exit(EXIT_FAILURE);
    }

    // Set reuse addr option to eliminate "Address already in use"
    // error in bind
    sock_opts = 1;
    if (-1 == setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &sock_opts,
			 sizeof(sock_opts))) {
	perror("setsockopt");
	exit(EXIT_FAILURE);
    }

    // Technically, getaddrinfo returns a linked list of addrs in
    // server_addt, but we are just using the first one.  See
    // the Beej docs, https://beej.us/guide/bgnet/html/#a-simple-stream-client
    if (bind(sock_fd, server_addr->ai_addr, server_addr->ai_addrlen)) {
	perror("bind");
	freeaddrinfo(server_addr);
	close(sock_fd);
	exit(EXIT_FAILURE);
    }

    freeaddrinfo(server_addr);

    if (listen(sock_fd, SOCKET_LISTEN_BACKLOG)) {
	perror("listen");
	close(sock_fd);
	exit(EXIT_FAILURE);
    }
    return sock_fd;
}

// Wait for connection from client.  Returns a new fd that client data can be
// read from, exit's on error.
int wait_for_client_connection(int sock_fd, char *client_ip_addr_str,
			       char *client_port_str)
{
    int conn_fd;
    struct sockaddr client_addr;
    socklen_t client_addr_len;

    // Wait for client connections.  accept can be interrupted with a
    // caught SIGINT or SIGTERM; log message if so.
    client_addr_len = sizeof(struct sockaddr);
    if (-1 == (conn_fd = accept(sock_fd, &client_addr, &client_addr_len))) {
	if (EINTR == errno) {
	    // System call, log message and exit cleanly.  Safe to exit,
	    // since there isn't a connection to clean up yet.
	    PRINTF("Caught signal, exiting\n");
	    close(sock_fd);
	    syslog(LOG_USER|LOG_INFO,"Caught signal, exiting");
	    exit(EXIT_SUCCESS);
	} else {
	    perror("accept");
	    close(sock_fd);
	    exit(EXIT_FAILURE);
	}
    } else {
	// conn_fd == new connection from client.
	// Use getnameinfo to parse client IP address and port number
	if (getnameinfo(&client_addr, sizeof(client_addr),
			client_ip_addr_str, IP_ADDR_MAX_STRLEN,
			client_port_str, IP_ADDR_MAX_STRLEN,
			NI_NUMERICHOST | NI_NUMERICSERV)) {
	    perror("getnameinfo");
	    shutdown(conn_fd, SHUT_RDWR);
	    close(conn_fd);
	    close(sock_fd);
	    exit(EXIT_FAILURE);
	}
	PRINTF("Accepted connection from %s:%s\n", client_ip_addr_str,
	       client_port_str);
	syslog(LOG_USER|LOG_INFO, "Accepted connection from %s",
	       client_ip_addr_str);
    }
    return(conn_fd);
}

int main(int argc, char *argv[])
{
    int sock_fd, conn_fd;
    int client_done;
    char client_ip_addr_str[IP_ADDR_MAX_STRLEN];
    char client_port_str[IP_ADDR_MAX_STRLEN];
    int ret_val = EXIT_SUCCESS;
    char *buf_start, *buf_curr;
    int cur_buf_size;
    int bytes_read;

    // Connect SIGINT and SIGTERM - perror and exit's on failure
    setup_signals();

    // Set up the socket with socket, bind, listen calls
    // perror and exit's on failure.  If we return, sock_fd is valid
    sock_fd = socket_init();

    // Only support one client connection at a time.  Could fork and
    // create a child process to handle simultaneous clients.
    while (EXIT_FAILURE != ret_val) {
	client_done = 0;
	conn_fd = wait_for_client_connection(sock_fd, client_ip_addr_str,
					     client_port_str);

	// Allocate an initial buffer.  buf_start is the original
	// buffer returned by malloc and used for realloc/free.
	// buf_curr is the current recv pointer, used by recv.
	cur_buf_size = SOCK_READ_BUF_SIZE;
	if (!(buf_curr = (buf_start = malloc(cur_buf_size)))) {
	    perror("malloc");
	    shutdown(conn_fd, SHUT_RDWR);
	    close(conn_fd);
	    ret_val = EXIT_FAILURE;
	}
	while (!client_done && (EXIT_FAILURE != ret_val)) {
	    // Number of bytes read is length of string (with
	    // newline), but no null terminator is added.
	    bytes_read = recv(conn_fd, buf_curr, SOCK_READ_BUF_SIZE, 0);
	    if (-1 >= bytes_read) {
		// -1 on error
		perror("recv");
		shutdown(conn_fd, SHUT_RDWR);
		close(conn_fd);
		ret_val = EXIT_FAILURE;
	    } else if (0 == bytes_read) {
		// 0 on remote connection closed
		shutdown(conn_fd, SHUT_RDWR);
		close(conn_fd);
		client_done = 1;
		PRINTF("Closed connection from %s:%s\n", client_ip_addr_str,
		       client_port_str);
		syslog(LOG_USER|LOG_INFO, "Closed connection from %s",
		       client_ip_addr_str);
	    } else if (SOCK_READ_BUF_SIZE == bytes_read) {
		// If the recv filled the buffer, there may be more data
		// waiting.  Enlarge the buffer and read some more.
		cur_buf_size += SOCK_READ_BUF_SIZE;
		if (!(buf_start = realloc(buf_start, cur_buf_size))) {
		    perror("realloc");
		    shutdown(conn_fd, SHUT_RDWR);
		    close(conn_fd);
		    ret_val = EXIT_FAILURE;
		}
		// Realloc can (will) move the buffer.  Calculate new
		// read destination as new buffer start plus current
		// size, then back up one read block size.
		buf_curr = buf_start + cur_buf_size - SOCK_READ_BUF_SIZE;
	    } else {
		// Read < SOCK_READ_BUF_SIZE and > 0.  We've read all
		// of the client data.  Null terminate the string,
		// write to the output file, and return the whole file
		buf_curr[bytes_read] = 0;
		PRINTF("length = %ld\n",strlen(buf_start));
	    }
	}
    }

    free(buf_start);		// Nop if NULL
    shutdown(conn_fd, SHUT_RDWR);
    close(conn_fd);
    close(sock_fd);

    return(ret_val);
}
