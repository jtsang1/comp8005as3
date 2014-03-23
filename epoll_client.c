/******************************************************************
File: 		epoll_client.c

Usage:		./epoll_client
			-h <server_address>
			-p <int_port>
			-c <int_connections>
			-d <string_data>
			-i <int_iterations>
			
Authors:	Jeremy Tsang
			Kevin Eng
			
Date:		February 10, 2014

Purpose:	COMP 8005 Assignment 2 - Comparing Scalable Servers - 
			threads/select()/epoll()
*******************************************************************/
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


#define BUFLEN 800
#define EPOLL_QUEUE_LEN 256 // Must be > 0. Only used for backward compatibility.

// Globals
int print_debug = 0;
int e_send = 0, e_recv = 0;
static void SystemFatal(const char* message);
struct timeval start, end;

struct custom_data{
	int fd;
	int total;		// Total number of messages to send and receive
	int sent;		// Number of messages sent
	int received;	// Number of messages received
};

void print_helper(){
	gettimeofday (&end, NULL);
	
	long bytes_sent = (long)e_send * BUFLEN;
	long bytes_recv = (long)e_recv * BUFLEN;
	
	float total_time = (float)(end.tv_sec - start.tv_sec) + ((float)(end.tv_usec - start.tv_usec)/1000000);

	//float avg_sent_msg_per_sec = (float)e_send/total_time;
	//float avg_sent_bytes_per_sec = (float)bytes_sent/total_time;
	
	float avg_recv_msg_per_sec = (float)e_recv/total_time;
	float avg_recv_bytes_per_sec = (float)bytes_recv/total_time;
	float avg_sec_per_recv_msg = total_time/(float)e_recv;
	
	printf("\r%-8.3f%-10d%-13ld%-10d%-13ld%-13.3f%-15.3f%-9.7f",\
	total_time,\
	e_send,\
	bytes_sent,\
	e_recv,\
	bytes_recv,\
	avg_recv_msg_per_sec,\
	avg_recv_bytes_per_sec,\
	avg_sec_per_recv_msg);
}

// Print client live stats
void * print_loop(){
	int c;
	char line[92];
	for(c = 0;c < 91;c++)
		line[c] = '-';
	line[c] = '\0';
	
	// Summary
	printf("\n%-8s%-10s%-13s%-10s%-13s%-13s%-15s%-9s\n",\
	"Time(s)",\
	"SentMsg",\
	"SentBytes",\
	"RecvMsg",\
	"RecvBytes",\
	"AvgMsg/s",\
	"AvgByte/s",\
	"AvgRTT(s)");
	printf("%s\n",line);
	
	while(1){
		
		print_helper();
		
		sleep(1);
	}
}

int main (int argc, char ** argv){
	
	gettimeofday (&start, NULL);	

	// Remove the need to flush printf with "\n" everytime...
	setbuf(stdout, NULL);

	/**********************************************************
	Parse input parameters
	**********************************************************/
	
	char * host, * data;
	int port, connections, iterations, c;

	int num_params = 0;
	while((c = getopt(argc, argv, "h:p:c:d:i:")) != -1)
	{
		switch(c)
		{
			case 'h':
			host = optarg;
			break;
			case 'p':
			port = atoi(optarg);
			break;
			case 'c':
			connections = atoi(optarg);
			break;
			case 'd':
			data = optarg;
			break;
			case 'i':
			iterations = atoi(
			optarg);
			break;
		}
		num_params++;
	}
	
	if(num_params < 4)
	{
		printf("\n\
Usage: ./epoll_client\n\
-h <address>\t\tSpecify host.\n\
-p <port>\t\tOptionally specify port.\n\
-c <connections>\tNumber of connections to use.\n\
-d <data>\t\tData to send.\n\
-i <iterations>\t\tNumber of iterations to use.\n\n");

		SystemFatal("params");
	}
	else
		if(print_debug == 1)
			printf("You entered -h %s -p %d -c %d -d %s -i %d\n",host, port, connections, data, iterations);
	
	/**********************************************************
	Epoll init. Create all sockets and add to epoll event loop
	**********************************************************/
	
	int i, * sd, arg = 1, epoll_fd;
	sd = malloc(sizeof(int) * connections);
	static struct epoll_event events[EPOLL_QUEUE_LEN];
	struct epoll_event * event = malloc(sizeof(struct epoll_event) * connections);
	struct custom_data * cdata = malloc(sizeof(struct custom_data) * connections);
	
	// Initialize server's sockaddr_in and hostent
	struct sockaddr_in server;
	struct hostent * hp;
	memset(&server, 0, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	if((hp = gethostbyname(host)) == NULL)
		SystemFatal("gethostbyname");
	bcopy(hp->h_addr, (char *)&server.sin_addr, hp->h_length);
	
	// Create epoll file descriptor
	if((epoll_fd = epoll_create(EPOLL_QUEUE_LEN)) == -1)
		SystemFatal("epoll_create");
		
	// Create all sockets
	for(i = 0; i < connections; i++){
	
		if((sd[i] = socket(AF_INET, SOCK_STREAM, 0)) == -1)
			SystemFatal("socket");
		
		if(print_debug == 1)
			fprintf(stdout,"socket() - sd: %d\n", sd[i]);
		
		// Set SO_REUSEADDR so port can be reused immediately
		if(setsockopt(sd[i], SOL_SOCKET, SO_REUSEADDR, &arg, sizeof(arg)) == -1)
			SystemFatal("setsockopt");
		
		// Make server socket non-blocking
		if(fcntl(sd[i], F_SETFL, O_NONBLOCK | fcntl(sd[i], F_GETFL, 0)) == -1)
			SystemFatal("fcntl");
		
		// Create data struct for each epoll descriptor
		cdata[i].fd = sd[i];
		cdata[i].total = iterations;
		cdata[i].sent = 0;
		cdata[i].received = 0;
		
		// Add all sockets to epoll event loop
		event[i].events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLET;
		event[i].data.ptr = (void *)&cdata[i]; // data is a union type
		
		if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sd[i], &event[i]) == -1)
			SystemFatal("epoll_ctl");
	}
	
	/**********************************************************
	Enter epoll event loop
	**********************************************************/
	int num_fds, f, bytes_to_read, n, s, timeout = 5000,
		fin = 0, e_err = 0, e_hup = 0, e_conn = 0, e_in = 0, e_out = 0;
	
	e_recv = 0;
	e_send = 0;
	
	pthread_t t1;
	pthread_create(&t1, NULL, &print_loop, NULL);
	
	char rbuf[BUFLEN], sbuf[BUFLEN];
	
	bytes_to_read = BUFLEN;
	strcpy(sbuf, data);
	
	while(1){
		
		// If all sockets are finished break out of while loop
		if(print_debug == 1)
			fprintf(stdout,"fin: %d e_err: %d e_hup: %d e_conn: %d e_in: %d e_out: %d e_recv: %d e_send: %d\n", fin, e_err,e_hup,e_conn,e_in,e_out,e_recv,e_send);
		if(fin == connections)
			break;
		
		num_fds = epoll_wait(epoll_fd, events, EPOLL_QUEUE_LEN, timeout);
		if(num_fds < 0)
			SystemFatal("epoll_wait");
		else if(num_fds == 0)
			break;
			
		if(print_debug == 1)
			fprintf(stdout,"num_fds: %d\n", num_fds);
			
		for(f = 0;f < num_fds;f++){
			
			// EPOLLERR - close socket and continue
			if(events[f].events & EPOLLERR){
				e_err++;
				
				// Retrieve cdata
				struct custom_data * ptr = (struct custom_data *)events[f].data.ptr;
				
				if(print_debug == 1)
					fprintf(stdout,"EPOLLERR - closing fd: %d\n", ptr->fd);
				close(ptr->fd);
				fin++;
				continue;
			}
			
			// EPOLLHUP
			if(events[f].events & EPOLLHUP){
				e_hup++;
				
				// Retrieve cdata
				struct custom_data * ptr = (struct custom_data *)events[f].data.ptr;
				
				if(print_debug == 1)
					printf("EPOLLHUP - fd: %d\n", ptr->fd);
				
				// Connect the socket if EPOLLHUP was generated by an unconnected socket
				if(ptr->sent == 0){
					
					if(connect(ptr->fd, (struct sockaddr *)&server, sizeof(server)) == -1){
						if(errno == EINPROGRESS) // Only connecting on non-blocking socket
							;
						else
							SystemFatal("connect");
					}
					
					int sock_error = 0;
					socklen_t len = sizeof(sock_error);
					
					getsockopt(ptr->fd, SOL_SOCKET, SO_ERROR, &sock_error, &len);
					if (sock_error == 0)
						e_conn++;
					
					
					if(print_debug == 1)
						printf("Connected: Server: %s\n", hp->h_name);
					//pptr = hp->h_addr_list;
					//printf("IP Address: %s\n", inet_ntop(hp->h_addrtype, *pptr, str, sizeof(str)));
					
				}
				else{
					if(print_debug == 1)
						fprintf(stdout,"EPOLLHUP - closing fd: %d\n", ptr->fd);
					close(ptr->fd);
					fin++;
					continue;
				}
			}
			
			// EPOLLIN
			if(events[f].events & EPOLLIN){
				e_in++;
				
				// Retrieve cdata
				struct custom_data * ptr = (struct custom_data *)events[f].data.ptr;
				
				if(print_debug == 1)
					fprintf(stdout,"EPOLLIN - fd: %d\n", ptr->fd);
				
				// Data on the fd waiting to be read. Data must be read completely,
				// since we are running in edge-triggered mode and won't get a notification
				// again for the same data
				while(1){
				
					n = 0;
					// Call recv once for specified amount of data
					n = recv(ptr->fd, rbuf, bytes_to_read,0);
					
					// Read fixed size message
					if(n == BUFLEN){
						e_recv++;
						// Increment receive counter
						ptr->received++;
						if(print_debug == 1)
							printf("(%d) Transmitted and Received: %s\n",getpid(), rbuf);
						if(print_debug == 1)
							printf("ptr.received: %d ptr.total: %d\n",ptr->received, ptr->total);
						// All messages received, close socket
						if(ptr->received == ptr->total){
							close(ptr->fd);
							fin++;
							break;
						}
					}
					// No more messages or read error
					else if(n == -1){
						if(errno != EAGAIN && errno != EWOULDBLOCK)
							perror("recv");
						
						break;
					}
					// Wrong message size or zero-length message 
					// or stream socket peer has performed an orderly shutdown
					else{
						break;
					}
				}
			}
			
			// EPOLLOUT
			if(events[f].events & EPOLLOUT){
				e_out++;
				
				// Retrieve cdata
				struct custom_data * ptr = (struct custom_data *)events[f].data.ptr;
				
				if(print_debug == 1)
					fprintf(stdout,"EPOLLOUT - fd: %d\n", ptr->fd);
				
				// Send one message and increase counter	
				if(ptr->sent == ptr->received && ptr->sent < ptr->total){
					s = 0;
					s = send(ptr->fd, sbuf, BUFLEN, 0);
					
					// Send fixed size message
					if(s == BUFLEN){
						e_send++;
						ptr->sent++;
					}
					else if(s == -1){
						if(errno != EAGAIN && errno != EWOULDBLOCK){
							if(print_debug == 1)
								perror("send");
						}
						else{
							if(print_debug == 1)
								perror("send non block");
						}
					}
					// Wrong number of bytes sent
					else{
						;
					}
					
				}
				
				// Remove EPOLL_OUT if all messages are sent
				if(ptr->sent == ptr->total){
					events[f].events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET;
		
					if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, ptr->fd, &events[f]) == -1)
						SystemFatal("epoll_ctl1");	
				}
			}
			
			//sleep(1);
		}
	}
	
	/**********************************************************
	Send and receive data
	**********************************************************/
	
	/*char rbuf[BUFLEN], sbuf[BUFLEN], * bp;
	int bytes_to_read, n;
	
	strcpy(sbuf, data);	
	//sbuf = data;
	//sbuf = "abcde";
	int i;
	
	//Send data <iterations> number of times 
	for(i = 0;i < iterations;i++)
	{
		//printf("Transmit: %s\t", data);
		
		send(sd, sbuf, BUFLEN, 0);
	
		//printf("Receive: ");
		bp = rbuf;
		bytes_to_read = BUFLEN;
	
		//make repeated calls to recv until there is no more data
		n = 0;
		while((n = recv(sd,bp,bytes_to_read,0)) < BUFLEN)
		{
	
			bp += n;
			bytes_to_read -= n;
	
		}
	
		printf("(%d) Transmitted and Received: %s\n",getpid(), rbuf);
	}
	fflush(stdout);
	
	//shutdown(sd, SHUT_RDWR);
	close(sd);*/
	
	/**********************************************************
	End epoll?
	**********************************************************/
	pthread_kill(t1,0);
	
	print_helper();
	printf("\n");
	
	if(print_debug == 1)		
		fprintf(stdout,"fin: %d e_err: %d e_hup: %d e_conn: %d e_in: %d e_out: %d e_recv: %d e_send: %d\n", fin, e_err,e_hup,e_conn,e_in,e_out,e_recv,e_send);
	
	free(sd);
	free(event);
	free(cdata);
	
	return 0;
}

static void SystemFatal(const char* message)
{
	perror(message);
	exit(EXIT_FAILURE);
}

