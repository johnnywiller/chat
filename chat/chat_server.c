#include <sys/socket.h> // sockets header
#include <stdio.h> // standard I/O
#include <stdlib.h> // common used library functions plus EXIT_SUCCESS and EXIT_FAILURE
#include <sys/un.h> 
#include <unistd.h> // prototypes for many system calls
#include <errno.h> // declares errno variables and error constants
#include <string.h> // string-handly functions
#include <fcntl.h>
#include <pthread.h> // posix threads
#include <stdbool.h> // boolean macros
#include <sys/epoll.h>
#include <string.h>


#define MAX_BUF 1024
#define MAX_EVENTS 1
#define MAX_CLIENTS 100


struct Client {
	char* nick;
	int fd;
};

static struct Client* clients[MAX_CLIENTS];  // handle clients 
static int clients_pos[MAX_CLIENTS];
static int connected_clients = 0;
static int fd_pipe[2];
static int fd_epoll;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; 

struct epoll_event event;
struct epoll_event events_list[MAX_EVENTS]; 

static void* send_messages_clients(void* arg);
static void* accept_new_connections(void* arg);
static void spawn_logger(int fd_pipe[2]);
static void remove_client(int client);
static void do_log(char* msg, int buflen, int client_pos);


int main(int argc, char* argv[]) {
	
	void* ret_socket_accept;
	void* ret_send_messages;

	pthread_t socket_accept;
	pthread_t send_messages;
	
	if ((fd_epoll = epoll_create1(0)) == -1) {
                perror("epoll");
                exit(EXIT_FAILURE);
        }

	if (pthread_create(&socket_accept, NULL, accept_new_connections, NULL) == -1) {
		perror("pthread create new connections");
		exit(EXIT_FAILURE);
	}
	if (pthread_create(&send_messages, NULL, send_messages_clients, NULL) == -1) {
		perror("pthread create send messages");
		exit(EXIT_FAILURE);
	}

	if (pthread_join(socket_accept, &ret_socket_accept) == -1) {
		perror("pthread join socket accept");
		exit(EXIT_FAILURE);
	}
	if (pthread_join(send_messages, &ret_send_messages) == -1) {
		perror("pthread join send messages");
		exit(EXIT_FAILURE);	
	}

	exit(EXIT_SUCCESS);
}

// create a fork() and spawns the logger process.
// change the STDIN of the logger to be our pipe 
static void spawn_logger(int fd_pipe[2]) {
	
	switch(fork()) {
	case -1: // handle error 
		perror("fork");
		break;
	case 0: // change STDIN to be the pipe's read fd and exec the logger bin
		if (dup2(fd_pipe[0], STDIN_FILENO) == -1) {
			perror("dup2");
			break;	
		}	
		
		// close the write pipe in the child
		close(fd_pipe[1]);

		char* argvec[2];
		argvec[0] = "logger"; 
		argvec[1] = NULL;
		
		// starts the chat logger 
		if (execve("chat_logger", argvec, NULL) == -1) {
			perror("execve");
			break;
		}
	default: // close read fd in the parent
		close(fd_pipe[0]);
		break;	
	}

}


static void do_log(char* msg, int buflen, int client_pos) {
	
	struct Client client = *clients[client_pos];
	char buf[buflen + strlen(client.nick) + 4];
	snprintf(buf, buflen + strlen(client.nick) + 4, "[%s] %s\n", client.nick, msg);
	if (write(fd_pipe[1], buf, strlen(buf)) == -1) {
		perror("write pipe");			
	}
}

static void send_to_others(char* msg, int buflen, int client_pos) {
	
	struct Client client = *clients[client_pos];
	char buf[buflen + strlen(client.nick) + 4];
	snprintf(buf, buflen + strlen(client.nick) + 4, "[%s] %s\n", client.nick, msg);
	
	for (int j = 0; j < connected_clients; j++) {
		if (client.fd != clients[j]->fd) // skip the sender 
			if (write(clients[j]->fd, buf, buflen + strlen(client.nick) + 4) == -1) { // write to other clients
				perror("write clients");	
			}			
				
	}
}

// removes the client by placing the last one in the actual position to be removed
static void remove_client(int fd_client) {
	
	int pos = clients_pos[fd_client];
	char* connected = "!Client has been disconnected!\n"; 	
	do_log(connected, strlen(connected), pos);

	// critical session of connected clients
	pthread_mutex_lock(&mutex);

	clients[pos] = clients[connected_clients];
	connected_clients--;

	pthread_mutex_unlock(&mutex);

	if (epoll_ctl(fd_epoll, EPOLL_CTL_DEL, fd_client, NULL) == -1) {
		perror("epoll_ctl del");
	}	
	close(fd_client);
}
static void* send_messages_clients(void* arg) {
	ssize_t numRead;
	char buf[MAX_BUF];
	int ready_fd;
	
	while(true) {
		
		if ((ready_fd = epoll_wait(fd_epoll, events_list, MAX_EVENTS, -1)) == -1) {
                        perror("epoll_wait");
                        exit(EXIT_FAILURE);
                }
		
                for (int i = 0; i < ready_fd; i++) {
                        
			if ((numRead = read(events_list[i].data.fd, buf, MAX_BUF)) == -1) {
                                perror("read");
                                continue;
                       
		       	} else if (numRead == 0) { // socket disconnected
				remove_client(events_list[i].data.fd);
				continue;				
			}

		       	do_log(buf, numRead, clients_pos[events_list[i].data.fd]);
			send_to_others(buf, numRead, clients_pos[events_list[i].data.fd]);
			
                }
	}


	pthread_exit(NULL);
}

static void* accept_new_connections(void* arg) {
	
	int fd, fd_socket, flags;

	int backlog = 10; // max pending connections at the same time

	struct sockaddr_un addr; // address of socket UNIX
	const char* ABS_SOCKET_NAME = "chat"; // used in Linux abstract socket namespace

	
	memset(&addr, 0, sizeof(struct sockaddr_un)); // clearing struct to ensure 
						      // that no nonstandard field remains set up. 
	
	// defines socket domain UNIX and specifies a socket name 
	addr.sun_family = AF_UNIX;
	
	strncpy(&addr.sun_path[1], ABS_SOCKET_NAME, strlen(ABS_SOCKET_NAME)); // using linux abstract socket namespace
	
	// create the socket
	if ((fd_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	 // bind struct to an address
	if (bind(fd_socket, (struct sockaddr*) &addr, sizeof(struct sockaddr_un)) == -1) {
		perror("socket bind");
		exit(EXIT_FAILURE);
	}
	
	if (listen(fd_socket, backlog) == -1) { // put the socket in LISTENING state
		perror("socket listen");
		exit(EXIT_FAILURE);
	}
	// create the pipe used to comunicate with logger process
	if (pipe(fd_pipe) == -1) {
		perror("pipe");
		exit(EXIT_FAILURE);
	}

	// spawns the logger process
	spawn_logger(fd_pipe);
	
	char* connected = "connected client\n"; 

	while(true) {	
		// accepts new connections	
		if ((fd = accept(fd_socket, NULL, NULL)) > 0) {			

			// put the recent connected socket to epoll monitor list
               	 	event.data.fd = fd;
	                event.events = EPOLLIN;
                	if (epoll_ctl(fd_epoll, EPOLL_CTL_ADD, fd, &event) == -1) {
                        	perror("epoll_ctl");
	                        exit(EXIT_FAILURE);
          	        }

			struct Client* client = malloc(sizeof(struct Client));
			client->nick = malloc(strlen("client-") + 3);
			sprintf(client->nick, "client-%d", fd);			
			client->fd = fd;
			
			pthread_mutex_lock(&mutex);
			clients[connected_clients] = client;			
			clients_pos[fd] = connected_clients;
						
			do_log(connected, strlen(connected), connected_clients);			
			connected_clients++;
			pthread_mutex_unlock(&mutex);
			
		}	
	}
	pthread_exit(NULL);
}








