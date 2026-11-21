#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h> //ADD

int sockfd, count;
fd_set afds, rfds, wfds;
char *out[FD_SETSIZE], *in[FD_SETSIZE];
int ids[FD_SETSIZE];
char *msg;
char buf_notify[64];
char buf_recv[1024];

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

void fatal_error(void) {
    write(2, "Fatal error\n", 12);
    free(msg);
    for (int fd = 0; fd < FD_SETSIZE; fd++)
        clean_fd(fd);
    exit(1);
}

//START COPY
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
				fatal_error(); //MODIFY
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
		fatal_error(); //MODIFY
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}
//END COPY

int add_fd(int fd) {
    if (fd < 0 || fd >= FD_SETSIZE) {
        if (fd >= 0)
            close(fd);
        return (0);
    }
    FD_SET(fd, &afds);
    return (1);
}

void noutify_outher(int outhor, char *text) {
    for (int fd = 0; fd < FD_SETSIZE; fd++)
        if (FD_ISSET(fd, &afds) && fd != sockfd && fd != outhor)
            out[fd] = str_join(out[fd], text);
}

void accept_client(void) {
    int fd = accept(sockfd, NULL, NULL);
    if (!add_fd(fd))
        return;
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
    in[fd] = str_join(in[fd], buf_recv);
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
    for (size_t i = 0; i <= len - (size_t)n; i++)
        out[fd][i] = out[fd][i + n];
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
    
    //START COPY
    FD_ZERO(&afds);      //ADD
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (!add_fd(sockfd)) //ADD
        fatal_error();   //ADD
    
    struct sockaddr_in servaddr;            //MODIFY
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(2130706433);
    servaddr.sin_port = htons(atoi(av[1])); //MODIFY

    if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) {
        fatal_error();
    }
    if (listen(sockfd, 10) != 0) {
        fatal_error();
    }
    //END COPY
    
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
// rfds → recv → in → extract_line → out → wfds → send
