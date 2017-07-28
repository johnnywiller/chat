#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#define MAX_BUF 1024
#define MAX_EVENTS 10

int main(int argc, char* argv[]) {

	int fd_epoll;
	int fd_logfile;
	int ready_fd;
	struct epoll_event event;
	struct epoll_event events_list[MAX_EVENTS];
	
	if ((fd_logfile = open("log.txt", O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP)) == -1) {
		perror("open");
		exit(EXIT_FAILURE);
	}
	char buf[MAX_BUF];
	int numRead;

	while (true) {
		
		if ((numRead = read(STDIN_FILENO, buf, MAX_BUF)) == -1) {
			perror("read");
			exit(EXIT_FAILURE);
		}

		write(fd_logfile, buf, numRead);
		
	}


	exit(EXIT_SUCCESS);
}
