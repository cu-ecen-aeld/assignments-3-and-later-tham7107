//////////////////////////////////////////////////////////////////////
//
// Thomas Ames
// ECEA 5305, assignment #5, socket.c
// July 2023
//

#define _GNU_SOURCE		// To get strchrnul

#include <stdio.h>
#include <stddef.h>
#include <signal.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/queue.h>
#include <time.h>

//#define DEBUG 1
#undef DEBUG

#define TCP_PORT "9000"
#define SOCKET_LISTEN_BACKLOG	5
#define IP_ADDR_MAX_STRLEN	20
#define TIME_MAX_STRLEN		100
#define SOCK_READ_BUF_SIZE	1000
#define DATAFILE		"/var/tmp/aesdsocketdata"
#define TIMESTAMP_DELAY_SECS	10

#ifdef DEBUG
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

// Thread data passed between main thread and server thread
struct server_thread_data {
    pthread_t thread_id;
    pthread_mutex_t *p_file_mutex;
    int file_fd;
    int conn_fd;
    // get rid of sock_fd. threads should not call cleaup routine directly
    int sock_fd;
    int client_done;
    char client_ip_addr_str[IP_ADDR_MAX_STRLEN];
    char client_port_str[IP_ADDR_MAX_STRLEN];
    SLIST_ENTRY(server_thread_data) entries;
};

// Thread data passed between main thread and server thread
struct timestamp_thread_data {
    pthread_t thread_id;
    pthread_mutex_t *p_file_mutex;
    int file_fd;
};

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
    getaddrinfo_hints.ai_family   = AF_INET;	 // IPv4
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
	    PRINTF("Caught signal in accept, exiting\n");
	    close(sock_fd);
	    unlink(DATAFILE);
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

int write_data_file(int fd, char * buf, int size)
{
    // Write up to (but NOT including) the newline (or '\0')
    // pointed to by newline_ptr.  Ignore partial writes (fs full)
    if (-1 == write(fd, buf, size)) {
	perror("write");
	return (-1);
    }
    // Now write out a trailing newline.  Done separately instead
    // of writing +1 above, in case we didn't get a newline in the
    // received packet (which shouldn't happen, since the
    // assignment says we always get newline
    buf[0] = '\n';
    if (-1 == write(fd, buf, 1)) {
	perror("write");
	return (-1);
    }
    return 0;
}

int send_data_file_to_client(int file_fd, int conn_fd, char * buf)
{
    int bytes_read;

    // Reset file pointer to start.  Should return 0 (requested
    // set point).  Non-zero is an error (either -1 for error
    // or some positive number if seek ends elsewhere.
    // Since the file is opened with O_APPEND, writes atomically
    // set the file pointer to the end before the write.
    if (lseek(file_fd, 0, SEEK_SET)) {
	perror("lseek");
	return (-1);
    }
    while ((bytes_read = read(file_fd, buf, SOCK_READ_BUF_SIZE))) {
	if (-1 == bytes_read) {
	    perror("read");
	    return (-1);
	}
	PRINTF("bytes_read = %d\n",bytes_read);
	// Ignore partial writes.  Shouldn't happen...
	if (write(conn_fd, buf, bytes_read) != bytes_read) {
	    perror("write");
	    return (-1);
	}
    }
    return 0;
}

// Server thread to handle a single connection from a client.
// Must free any resources allocated in the thread (ie malloc buffer).
// Connection socket, etc allocated in main are cleaned up in main
// thread after server thread exits.
void *server_thread(void *arg)
{
    struct server_thread_data *p_thread_data = (struct server_thread_data *)arg;
    sigset_t signal_set;
    char *buf_start, *buf_curr;
    int cur_buf_size;
    int bytes_read;
    char *newline_ptr;

    // Block SIGINT and SIGTERM - let main thread handle them.
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    sigaddset(&signal_set, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &signal_set, NULL)) {
	perror("pthread_sigmask");
	pthread_exit(p_thread_data);
    }

    // Allocate an initial buffer.  buf_start is the original
    // buffer returned by malloc and used for realloc/free.
    // buf_curr is the current recv pointer, used by recv.
    cur_buf_size = SOCK_READ_BUF_SIZE;
    if (!(buf_curr = (buf_start = malloc(cur_buf_size)))) {
	perror("malloc");
	pthread_exit(p_thread_data);
    }

    while (!p_thread_data->client_done) {
	bytes_read = recv(p_thread_data->conn_fd, buf_curr, SOCK_READ_BUF_SIZE, 0);
	if (-1 >= bytes_read) {
	    // -1 on error
	    perror("recv");
	    free(buf_start);
	    pthread_exit(p_thread_data);
	} else if (0 == bytes_read) {
	    // 0 on remote connection closed
	    p_thread_data->client_done = 1;
	    free(buf_start);
	    PRINTF("Closed connection from %s:%s\n", p_thread_data->client_ip_addr_str, p_thread_data->client_port_str);
	    syslog(LOG_USER|LOG_INFO, "Closed connection from %s",
		   p_thread_data->client_ip_addr_str);
	    pthread_exit(p_thread_data);
	} else if (SOCK_READ_BUF_SIZE == bytes_read) {
	    // If the recv filled the buffer, there may be more data
	    // waiting.  Enlarge the buffer and read some more.
	    cur_buf_size += SOCK_READ_BUF_SIZE;
	    if (!(buf_start = realloc(buf_start, cur_buf_size))) {
		perror("realloc");
		// Realloc free'd old buffer, nothing to free now
		pthread_exit(p_thread_data);
	    }
	    // Realloc can move the buffer.  Calculate new read
	    // destination as new buffer start plus current size,
	    // then back up one read block size.
	    buf_curr = buf_start + cur_buf_size - SOCK_READ_BUF_SIZE;
	} else {
	    // Read < SOCK_READ_BUF_SIZE and > 0.  We've read all
	    // of the client data.  Null terminate the string,
	    // write to the output file, and return the whole file
	    buf_curr[bytes_read] = 0;
	    PRINTF("length = %ld\n",strlen(buf_start));
	    // Find the first newline, start at byte 0.  If no newline,
	    // use the null at the end.
	    newline_ptr = strchrnul(buf_start,'\n');
	    PRINTF("strchr found newline at offset %ld\n", newline_ptr -
		   buf_start);

	    // Lock mutex around file i/o - can we unlock before read?
	    if (pthread_mutex_lock(p_thread_data->p_file_mutex)) {
		perror("pthread_mutex_lock");
		free(buf_start);
		pthread_exit(p_thread_data);		
	    }
	    

	    if (write_data_file(p_thread_data->file_fd, buf_start,
				newline_ptr - buf_start) ||
		send_data_file_to_client(p_thread_data->file_fd,
					 p_thread_data->conn_fd,
					 buf_start)) {
		free(buf_start);
		pthread_exit(p_thread_data);		
	    }

	    // Unlock mutex around file i/o - can we unlock before read?
	    if (pthread_mutex_unlock(p_thread_data->p_file_mutex)) {
		perror("pthread_mutex_unlock");
		free(buf_start);
		pthread_exit(p_thread_data);		
	    }

	    // Now read the entire file and send it back to the client
	    //
	    // Reset the buffer size and pointers
	    cur_buf_size = SOCK_READ_BUF_SIZE;
	    if (!(buf_curr = (buf_start = realloc(buf_start,
						  cur_buf_size)))) {
		perror("realloc");
		pthread_exit(p_thread_data);		
	    }
	}
    }
    // Don't really care about return, since we save the ptr in an SLIST
    return arg;
}

// Append a timestamp to the output file every TIMESTAMP_DELAY_SECS
// Re-use the server thread's struct thread_data, since it has the
//
// Must free any resources allocated in the thread (ie malloc buffer).
// Connection socket, etc allocated in main are cleaned up in main
// thread after server thread exits.
void *timestamp_thread(void *arg)
{
    struct timestamp_thread_data *p_thread_data =
	(struct timestamp_thread_data *) arg;
    struct timespec ts;
    struct tm tm;
    char timestr[TIME_MAX_STRLEN];
    size_t timelen;

    while (1) {
	// Returns a pointer to the supplied struct timespec,
	// ts.tv_sec is clock time in sec since epoch
	if (clock_gettime(CLOCK_REALTIME, &ts)) {
	    perror("clock_gettime");
	    pthread_exit(p_thread_data);
	}

	// Returns a pointer to the supplied struct timespec
	(void) gmtime_r(&(ts.tv_sec), &tm);

	timelen = TIME_MAX_STRLEN;
	timelen = strftime(timestr, timelen,
			   "timestamp:%Y-%m-%d %H:%M:%S%n", &tm);

	// Lock mutex around file i/o - can we unlock before read?
	if (pthread_mutex_lock(p_thread_data->p_file_mutex)) {
	    perror("pthread_mutex_lock");
	    pthread_exit(p_thread_data);
	}

	if (-1 == write(p_thread_data->file_fd, timestr, timelen)) {
	    perror("write");
	    pthread_exit(p_thread_data);
	}

	// Unlock mutex around file i/o - can we unlock before read?
	if (pthread_mutex_unlock(p_thread_data->p_file_mutex)) {
	    perror("pthread_mutex_unlock");
	    pthread_exit(p_thread_data);
	}
	PRINTF("TICK!\n");
	sleep(TIMESTAMP_DELAY_SECS);
    }
    return p_thread_data;
}

int main(int argc, char *argv[])
{
    int file_fd=0, sock_fd=0, conn_fd=0;
    int arg, daemonize;
    pthread_mutex_t datafile_mutex;
    SLIST_HEAD(slisthead, server_thread_data) thread_list_head;
    struct timestamp_thread_data *p_timestamp_thread_data;
    struct server_thread_data *p_server_thread_data=NULL;
    char client_ip_addr_str[IP_ADDR_MAX_STRLEN];
    char client_port_str[IP_ADDR_MAX_STRLEN];
    int exit_status = EXIT_FAILURE; // Fail by default

    if (-1 == (file_fd = open(DATAFILE, O_CREAT | O_APPEND | O_TRUNC | O_RDWR,
			      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH))) {
	perror("open");
	goto close_file_fd;
    }

    // Connect SIGINT and SIGTERM - perror and exit's on failure
    setup_signals();

    // Set up the socket with socket, bind, listen calls
    // perror and exit's on failure.  If we return, sock_fd is valid
    sock_fd = socket_init();

    // Now that we have successfully determined that we can bind to the
    // socket, call getopts to look for -d.
    // See: https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
    opterr = 0;			// Turn off getopt printfs
    daemonize = 0;		// Assume not until we find -d in argv
    while ((arg = getopt (argc, argv, "d")) != -1)
	switch (arg)
	{
	case 'd':
	    daemonize = 1;
	    break;
	// Ignore unknown opts and errors
	case '?':
	default:
	    break;
	}

    if (daemonize) {
	PRINTF("Daemonize...\n");
	// daemon(3) handles fork/setsid/chdir/redir of stdin/out/err to
	// /dev/null.  It does not close other open fd's (our sockets
	// or /var/tmp/aesdsocketdata).  Does not return in parent.
	if (-1 == daemon(0, 0))  // chdir /, dup2 stdin/out/err
	{
	    // Error - very very unlikely - system is whacked
	    perror("daemon");
	    goto close_sock_fd;
	}
	// Now running in child...
    }

    if (pthread_mutex_init(&datafile_mutex, NULL)) {
	perror("pthread_mutex_init");
	goto close_sock_fd;
    }

    SLIST_INIT(&thread_list_head);

    if (!(p_timestamp_thread_data =
	  malloc(sizeof(struct timestamp_thread_data)))) {
	perror("malloc");
	goto close_sock_fd;
    }

    p_timestamp_thread_data->p_file_mutex = &datafile_mutex;
    p_timestamp_thread_data->file_fd      = file_fd;

    if (pthread_create(&(p_timestamp_thread_data->thread_id), NULL,
		       timestamp_thread, (void *) p_timestamp_thread_data)) {
	perror("pthread_create");
	goto free_thread_data;
    } else {
	PRINTF("timestamp pthread_create successful\n");
    }

    while (1) {
	conn_fd = wait_for_client_connection(sock_fd, client_ip_addr_str,
					     client_port_str);

	if (!(p_server_thread_data = malloc(sizeof(struct server_thread_data)))) {
	    perror("malloc");
	    goto close_conn_fd;
	}

	p_server_thread_data->p_file_mutex = &datafile_mutex;
	p_server_thread_data->file_fd      = file_fd;
	p_server_thread_data->conn_fd      = conn_fd;
	p_server_thread_data->sock_fd      = sock_fd;
	p_server_thread_data->client_done  = 0;
	strcpy(p_server_thread_data->client_ip_addr_str, client_ip_addr_str);
	strcpy(p_server_thread_data->client_port_str, client_port_str);
	
	if (pthread_create(&(p_server_thread_data->thread_id), NULL,
			   server_thread, (void *) p_server_thread_data)) {
	    perror("pthread_create");
	    goto free_thread_data;
	} else {
	    PRINTF("pthread_create successful, p_server_thread_data = %p\n",
		   p_server_thread_data);
	}

	// Insert the thread_data structure into the list
	SLIST_INSERT_HEAD(&thread_list_head, p_server_thread_data, entries);

	// Now iterate over the list and join any that are done.
	// This probably has a race condition - if the last thread
	// completes while blocked in an accept, we will never join
	// it.  This is probably the srouce of the "You may see one
	// possibly lost message which looks like this, which it’s
	// OK to ignore" message in the assignment.
	SLIST_FOREACH(p_server_thread_data, &thread_list_head, entries) {
	    PRINTF("SLIST_FOREACH p_server_thread_data = %p\n", p_server_thread_data);
	    if (p_server_thread_data->client_done) break;
	}

	if (p_server_thread_data && p_server_thread_data->client_done) {
	    SLIST_REMOVE(&thread_list_head, p_server_thread_data,
			 server_thread_data, entries);
	    PRINTF("Removed list element = %p\n", p_server_thread_data);

	    // Join finished thread and cleanup previously allocated resources
	    if (pthread_join(p_server_thread_data->thread_id, NULL)) {
		perror("pthread_join");
		goto free_thread_data;
	    }
	    shutdown(p_server_thread_data->conn_fd, SHUT_RDWR);
	    close(p_server_thread_data->conn_fd);
	    free(p_server_thread_data);
	}
    }

    // Goto's are bad.  But if they are good enough for error handling/
    // shutdown in the kernel, they're good enough for me.
    // Should iterate and clear all server threads and also timestamp thread
free_thread_data: free(p_server_thread_data);
close_conn_fd:    shutdown(conn_fd, SHUT_RDWR); close(conn_fd);
close_sock_fd:    close(sock_fd);
close_file_fd:    close(file_fd); unlink(DATAFILE);
    exit(exit_status);
}
