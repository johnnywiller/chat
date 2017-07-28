#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>

int fd_socket;

static void* read_stdin(void* arg) {	
	ssize_t numRead;
	char buf[1024];

	while((numRead = read(STDIN_FILENO, buf, sizeof(buf))) > 0) 
		write(fd_socket, buf, numRead);
}

static void* read_socket(void* arg) {
	ssize_t numRead;
	char buf[1024];
	struct sockaddr_un addr;
	//const char* SERVER_ADDR = "/tmp/chat_socket";

	const char* ABS_SERVER_ADDR = "chat";
	
	memset(&addr, 0, sizeof(struct sockaddr_un));

	addr.sun_family = AF_UNIX;
	strncpy(&addr.sun_path[1], ABS_SERVER_ADDR, strlen(ABS_SERVER_ADDR));

	fd_socket = socket(AF_UNIX, SOCK_STREAM, 0);

	connect(fd_socket, (struct sockaddr*) &addr, sizeof(struct sockaddr_un));

	setbuf(stdout, NULL);

	while((numRead = read(fd_socket, buf, sizeof(buf))) > 0) 
		write(STDOUT_FILENO, buf, numRead);
	
}


int main(int argc, char* argv[]) {
	
	pthread_t pread_stdin;
	pthread_t pread_socket;

	void* ret_read_stdin;
	void* ret_read_socket;

	pthread_create(&pread_stdin, NULL, read_stdin, NULL);
	pthread_create(&pread_socket, NULL, read_socket, NULL);
	
	pthread_join(pread_stdin, &ret_read_stdin);
	
	exit(EXIT_SUCCESS);
}




