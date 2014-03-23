/*******************************************************************************
File: 		port_forwarder.c

Usage:				
	
Authors:	Jeremy Tsang, Kevin Eng		
	
Date:		March 22, 2014

Purpose:	COMP 8005 Assignment 3 - Basic Application Level Port Forwarder

*******************************************************************************/
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>



/*******************************************************************************
Definitions
*******************************************************************************/
#define TRUE 				1
#define FALSE 				0
#define EPOLL_QUEUE_LEN			256
#define BUFLEN				800


/* cinfo for storing client socket info*/
typedef struct{
	int fd;		// Socket descriptor
	int fd_pair;	// Corresponding socket to forward to
}cinfo;


/* sinfo for storing server socket info */
typedef struct{
	int fd;		// Socket descriptor
	char * server;	// Server to forward to
	int server_port;// Server port
}sinfo;



/*******************************************************************************
Globals and Prototypes
*******************************************************************************/
/* Globals */
sinfo ** servers = NULL;	// Array of server sockets listening
int servers_size = 0;


/* Function prototypes */
static void SystemFatal (const char* message);
static int ClearSocket (int fd);
void close_server (int);
sinfo * is_server(int fd);



/*******************************************************************************
Functions
*******************************************************************************/
/* Main */
int main (int argc, char* argv[]) {

	int i, arg; 
	int num_fds, epoll_fd;
	static struct epoll_event events[EPOLL_QUEUE_LEN], event;
	struct sigaction act;
	
	// set up the signal handler to close the server socket when CTRL-c is received
   	act.sa_handler = close_server;
    	act.sa_flags = 0;
    	if ((sigemptyset (&act.sa_mask) == -1 || sigaction (SIGINT, &act, NULL) == -1)){
		perror ("Failed to set SIGINT handler");
		exit (EXIT_FAILURE);
	}
	
	// Create the epoll file descriptor
	epoll_fd = epoll_create(EPOLL_QUEUE_LEN);
	if (epoll_fd == -1) 
		SystemFatal("epoll_create");
	
	// Read config file and create all listening sockets and add to epoll
	FILE * fp;
	ssize_t read;
	size_t len = 0;
	char * line = NULL;
	
	fp = fopen("port_forwarder.conf","r");
	while((read = getline(&line, &len, fp)) != -1){
		
		// Tokenize each line into array
		char * token;
		char * config[3];
		int config_index = 0;
		
		token = strtok(line,",");
		while(token != NULL){
			config[config_index++] = token;
			token = strtok(NULL,",");
		}
		//printf("Tokenized into %d\n",config_index);
		
		// Info from each line
		int port = atoi(config[0]);
		char * server = config[1];
		int server_port = atoi(config[2]);
		
		// Create the listening socket
		int fd_server;

		fd_server = socket (AF_INET, SOCK_STREAM, 0);
		if (fd_server == -1) 
			SystemFatal("socket");
	
		// set SO_REUSEADDR so port can be reused immediately after exit, i.e., after CTRL-c
		arg = 1;
		if (setsockopt (fd_server, SOL_SOCKET, SO_REUSEADDR, &arg, sizeof(arg)) == -1) 
			SystemFatal("setsockopt");
	
		// Make the server listening socket non-blocking
		if (fcntl (fd_server, F_SETFL, O_NONBLOCK | fcntl (fd_server, F_GETFL, 0)) == -1) 
			SystemFatal("fcntl");
	
		// Bind to the specified listening port
		struct sockaddr_in addr;
		memset (&addr, 0, sizeof (struct sockaddr_in));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(port);
		
		printf("Listening on port %d...\n",port);
		
		if (bind (fd_server, (struct sockaddr*) &addr, sizeof(addr)) == -1) 
			SystemFatal("bind");
	
		// Listen for fd_news; SOMAXCONN is 128 by default
		if (listen (fd_server, SOMAXCONN) == -1) 
			SystemFatal("listen");
	
		// Add to server list
		sinfo * server_sinfo = malloc(sizeof(sinfo));
		server_sinfo->fd = fd_server;
		server_sinfo->server = server;
		server_sinfo->server_port = server_port;
		
		servers = realloc(servers,sizeof(sinfo *) * servers_size);
		servers[servers_size++] = server_sinfo;
	
		// Add the server socket to the epoll event loop with it's data
		event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET;
		
		cinfo * server_cinfo = malloc(sizeof(cinfo));
		server_cinfo->fd = fd_server;
		event.data.ptr = (void *)server_cinfo;
	
		if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, fd_server, &event) == -1)
			SystemFatal("epoll_ctl");
	}
	
	if(line)
		free(line);
	
	fclose(fp);
    
	// Execute the epoll event loop
	while (TRUE){
	
		//fprintf(stdout,"epoll wait\n");
		
		num_fds = epoll_wait (epoll_fd, events, EPOLL_QUEUE_LEN, -1);
		if (num_fds < 0)
			SystemFatal ("epoll_wait");

		for (i = 0; i < num_fds; i++){
			
	    		// EPOLLHUP
	    		if (events[i].events & EPOLLHUP){
	    		
	    			// Get socket cinfo
	    			cinfo * c_ptr = (cinfo *)events[i].data.ptr;
    		
				fprintf(stdout,"EPOLLHUP - closing fd: %d\n", c_ptr->fd);
				
				close(c_ptr->fd);
				//free(c_ptr);
				
				continue;
			}
			
			// EPOLLERR
			if (events[i].events & EPOLLERR){
			
				// Get socket cinfo
    				cinfo * c_ptr = (cinfo *)events[i].data.ptr;
			
				fprintf(stdout,"EPOLLERR - closing fd: %d\n", c_ptr->fd);
				
				close(c_ptr->fd);
				
				continue;
			}
			
	    		assert (events[i].events & EPOLLIN);
	    						
	    		// EPOLLIN
	    		if (events[i].events & EPOLLIN){
    				
				// Get socket cinfo
				cinfo * c_ptr = (cinfo *)events[i].data.ptr;
	    			
				// Server is receiving one or more incoming connection requests
				sinfo * s_ptr = NULL;
				if ((s_ptr = is_server(c_ptr->fd)) != NULL){
				
					printf("EPOLLIN - incoming connection fd:%d\n",s_ptr->fd);
					
					while(1){
								
						struct sockaddr_in in_addr;
						socklen_t in_len;
						int fd_new = 0;
						
						//memset (&in_addr, 1, sizeof (struct sockaddr_in));
						fd_new = accept(s_ptr->fd, (struct sockaddr *)&in_addr, &in_len);
						if (fd_new == -1){
							// If error in accept call
							if (errno != EAGAIN && errno != EWOULDBLOCK)
								SystemFatal("accept");//perror("accept");
								
							perror("wat");
							// All connections have been processed
							break;
						}
						
						printf("EPOLLIN - connected fd: %d\n", fd_new);
						
						// Make the fd_new non-blocking
						if (fcntl (fd_new, F_SETFL, O_NONBLOCK | fcntl(fd_new, F_GETFL, 0)) == -1) 
							SystemFatal("fcntl");
				
						event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET;
						
						cinfo * client_info = malloc(sizeof(cinfo));
						client_info->fd = fd_new;
						event.data.ptr = (void *)client_info;
						
						if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, fd_new, &event) == -1) 
							SystemFatal ("epoll_ctl");

						continue;
					}
				}
				// Else one of the sockets has read data
				else{
					fprintf(stdout,"EPOLLIN - read fd: %d\n", c_ptr->fd);
					
					if (!ClearSocket(c_ptr->fd)){
						// epoll will remove the fd from its set
						// automatically when the fd is closed
						close(c_ptr->fd);
					}
				}
			}
		}
	}
	
	exit (EXIT_SUCCESS);
}


/* Read Buffer */
static int ClearSocket (int fd) {
	int n, bytes_to_read, m = 0;
	char *bp, buf[BUFLEN];
		
	bp = buf;
	bytes_to_read = BUFLEN;
	
	// Edge-triggered event will only notify once, so we must
	// read everything in the buffer
	while(1){
		n = recv (fd, bp, bytes_to_read, 0);
	
		// Read message
		if(n > 0){
			m++;
			
			printf ("Read (%d) bytes on fd %d:\n%s\n", n, fd, buf);
			
			/*int l = send(fd, buf, BUFLEN, 0);
			if(l == -1){
				
			}*/
		}
		// No more messages or read error
		else if(n == -1){
			if(errno != EAGAIN && errno != EWOULDBLOCK)
				perror("recv");
			
			break;
		}
		// Wrong message size or zero-length message
		// Stream socket peer has performed an orderly shutdown
		else{
			printf ("Shutdown on fd %d\n", fd);
			break;
		}
	}
	
	//printf ("sending m:%d\n", m);
	
	if(m == 0)
		return FALSE;
	else
		return TRUE;
	/*
	while ((n = recv (fd, bp, bytes_to_read, 0)) > 0)
	{
		bp += n;
		bytes_to_read -= n;
		m++;
	}
	
	if(m == 0)
		return FALSE;
	
	//printf ("sending:%s\tloops:%d\n", buf, m);
	printf ("sending:%s\n", buf);

	send (fd, buf, BUFLEN, 0);
	//close (fd);
	return TRUE;*/
}


/* Prints the error stored in errno and aborts the program. */
static void SystemFatal(const char* message) {
    perror (message);
    exit (EXIT_FAILURE);
}


/* Server closing function, signalled by CTRL-C. */
void close_server (int signo){
    	int c = 0;
    	for(;c < servers_size;c++){
		close(servers[c]->fd);
    	}
	exit (EXIT_SUCCESS);
}


/* Check if fd is a server socket. */
sinfo * is_server(int fd){
	int c = 0;
	for(;c < servers_size;c++){
		if(servers[c]->fd == fd)
			return servers[c];
	}
	return NULL;
}

