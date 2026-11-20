#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <sys/select.h>

int afds[FD_SETSIZE];
int rfds[FD_SETSIZE];
int wfds[FD_SETSIZE];

char in[FD_SETSIZE];
char out[FD_SETSIZE];

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
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

void fatal_error() {
	write(2, "Fatal error\n", 11);
	for (int n = 0; n < FD_SETSIZE; n++) {
		free(in[n]);
		free(out[n]);
		in[n] = out[n] = NULL;
	}
	exit(1);
}

int main(int ac, char **av) {
	if (ac < 2) {
		write(2, "Wrong number of arguments\n", 26);
		return (1);
	}
	FD_ZERO(&afds);

	int connfd;
	struct sockaddr_in servaddr; 

	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	add_fd(sockfd);

	bzero(&servaddr, sizeof(servaddr));  
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433);
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
		for (int n = 0; n < FD_SETSIZE; n++)
			if (out[n])
				FD_SET(n, &wfds);
		
		if (select(rfds, wfds, NULL, NULL) <= 0) {
			fatal_error();
		}

		if (FD_ISSET(&rfds) && sockfd) {
			accept_client
		}

		for (int n =0; n < fD_SETSIZE; n++) {
			if (rfds[n])
				append_message

			if (wfds[n])
				flush_output
		
		}
	
	}
}
