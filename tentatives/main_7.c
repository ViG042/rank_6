#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>

fd_set afds, rfds, wfds;
int sockfd, count;
char *out[FD_SETSIZE], *in[FD_SETSIZE];
int ids[FD_SETSIZE];
char buf_notify[64];
char buf_recv[1024];
char *msg;

void clean_fd(int fd) {
	free(in[fd]);
	free(out[fd]);
	in[fd] = NULL;
	out[fd] = NULL;
	if (FD_ISSET(fd, &afds))
		close(fd);
	FD_CLR(fd, &afds);
	FD_CLR(fd, &wfds);
}

void fatal_error() {
	write(2, "Fatal error\n", 12);
	free(msg);
	for (int fd = 0; fd < FD_SETSIZE; fd++)
		clean_fd(fd);
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
				fatal_error();
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

int add_fd(int fd) {
	if (fd < 0 || fd >= FD_SETSIZE) {
		if (fd >= FD_SETSIZE)
			close(fd);
		return (0);
	}
	FD_SET(fd, &afds);
	return (1);
}

void noutify_outher(int author, char *text) {
	for (int fd = 0; fd < FD_SETSIZE; fd++)
		if (FD_ISSET(fd, &afds) && fd != sockfd && fd != author)
			out[fd] = str_join(out[fd], text); /////////
}

void accept_client() {
	int fd = accept(sockfd, NULL, NULL);
	if (!add_fd(fd)) {
		return;
	}
	ids[fd] = count++;
	sprintf(buf_notify, "server: client %d just arrived\n", ids[fd]);
	noutify_outher(fd, buf_notify);
}

void recv_in(int fd) {
	int n = recv(fd, buf_recv, sizeof(buf_recv) - 1, MSG_DONTWAIT);
	if (n <= 0) {
		if (n == 0) {
			clean_fd(fd);
			sprintf(buf_notify, "server: client %d just left\n", ids[fd]);
			noutify_outher(fd, buf_notify);
		}
		return;
	}
	buf_recv[n] = '\0';
	in[fd] = str_join(in[fd], buf_recv); ////////////////
	while (extract_message(&in[fd], &msg)) {
		sprintf(buf_notify, "client %d: ", ids[fd]);
		noutify_outher(fd, buf_notify);
		noutify_outher(fd, msg);
		free(msg);
	}
}

void send_out(int fd) {
	size_t len = strlen(out[fd]);
	ssize_t n = send(fd, out[fd], len, MSG_DONTWAIT | MSG_NOSIGNAL);
	if (n <= 0)
		return;
	for (size_t i = 0; i <= len - (size_t)n; i++) {
		out[fd][i] = out[fd][i + n];
	}
	if (!out[fd][0]) {
		free(out[fd]);
		out[fd] = NULL;
	}
}

int main(int ac, char **av) {
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
			if (FD_ISSET(fd, &rfds)) {
				if (fd == sockfd)
					accept_client();
				else
					recv_in(fd);
			}
			if (FD_ISSET(fd, &wfds))
				send_out(fd);
		}
	}	
}
