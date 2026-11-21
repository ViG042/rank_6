#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>

int sockfd;
fd_set afds, rfds, wfds;
char *out[FD_SETSIZE];
char *in[FD_SETSIZE];
int count;
int ids[FD_SETSIZE];
char buf_notify[64];
char buf_recv[1024];
char *msg;

void fatal_error() {
	write(2, "Fatal error\n", 12);
	for (int fd = 0, fd < FD_SETSIZE, fd++) {
		free(out[fd]);
		free(in[fd]);
		out[fd] = NULL;
		in[fd] = NULL;
		close(fd);
	}
	free(msg);
	msg = NULL;
	exit(1);
}

int extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int	i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		fatal_error();
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}
				
void send_out(int fd) {
	size_t len = strlen(out[fd]);
	ssize_t n = send(fd, out[fd], len, MSG_DONTWAIT | MSG_NOSIGNAL);
	if (n <= 0)
		return;
	for (size_t i = 0; i <= len - (size_t)n; i++)
		out[fd][i] = out[fd][i + n];
	if (!out[fd][0]) {
		free(out[fd]);
		out[fd] = NULL;
	}
}

void remove_client(int fd) {
	FD_CLR(&afds);
	FD_CLR(&wfds);
	free(in[fd]);
	free(out[fd]);
	in[fd] = NULL;
	out[fd] = NULL;
	close(fd);
	sprintf(notify_other, "server: client %d just left\n", ids[fd]);
	notify_other(fd, buf_notify);
}

void recv_in(int fd) {
	int n = recv(fd, buf_recv, sizeof(buf_recv) - 1, MSG_DONTWAIT);
	if (n <= 0) {
		if (n == 0)
			remove_client(fd);
		return;
	}
	buf_recv[n] = '\0';
	in[fd] = str_join(in[fd], buf_recv);
	while (extract_message(&in[fd], &msg)) {
		sprintf(buf_notify ,"client %d: ", ids[fd]);
		notify_other(fd, buf_notify);
		notify_other(fd, msg);
		free(msg);
	}
}

int add_fd(int fd) {
	if (fd < 0 || fd >= FD_SETSIZE) {
		if (fd > 0)
			close(fd);
		return (0);
	}
	FD_SET(fd, &afds);
	return (1);
}

void notify_other(int author, char *text)
	for (int fd = 0; fd < FD_SETSIZE; fd++) {
		if (fd != author && fd != sockfd && FD_ISSET(fd, &afds))
			out[fd] = str_join(out[fd], text);
}

void accept_client() {
	int fd = accept(sockfd, NULL, NULL);
	if (!add_fd(fd))
		return ;
	ids[fd] = count++;
	sprintf(buf_write, "server: client %d just arrived\n", ids[fd]);
	notify_other(fd, buf_write);
}

int main(int ac, char *av) {
	if (ac < 2) {
		write(2, "Wrong number of arguments\n", 26);
		return (1);
	}
	FD_ZERO(&afds);
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (!add_fd(sockfd))
		fatal_error();
	
	struct sockaddr_in servaddr; 
	bzero(&servaddr, sizeof(servaddr)); 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(av[1])); 

	if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) { 
		fatal_error();
	} 
	if (listen(sockfd, 10) != 0) {
		fatal_error(); 
	}
	while (1) {
		rfds = afds;
		FD_ZERO(&wfds);
		for (int fd = 0; fd < FD_SETSIZE; fd++) {
			if (out[fd])
				FD_SET(fd, &wfds);
		}
		if (select(FD_SETSIZE, &rfds, &wfds, NULL, NULL) < 0) {
			fatal_error();
		}
		for (int fd = 0; fd < FD_SETSIZE; fd++) {
			if (FD_ISSET(fd, &rfds) {
				if (fd == sockfd)
					accept_client();	
				else
					receive_message(fd);
			}
			if (FD_ISSET(fd, &wfds))
				flush_output(fd);
		}
	}
}
