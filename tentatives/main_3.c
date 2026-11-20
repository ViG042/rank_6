#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

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


int main(int ac, char** av) {
	if (ac < 2) {
		write(2, "Wrong number of arguments\n", 26);
		return (1);
	}

	FD_ZERO(&afds);	
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	add_fd(sockfd);

	struct sockaddr_in servaddr; 
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

	while(1) {
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
					receive_message(fd);
			}
			if (FD_ISSET(fd, &wfds))
				flush_output(fd);
		}
		
	}
}
