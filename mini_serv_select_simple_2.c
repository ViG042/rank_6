//START COPY
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
//END COPY

#include <sys/select.h> // A ajouter : select, fd_set et FD_SETSIZE.

int count, sockfd;
int ids[FD_SETSIZE];
char *in[FD_SETSIZE], *out[FD_SETSIZE];
char *msg; /* Ligne extraite : reste accessible en cas d'erreur. */
fd_set rfds, wfds, afds;
char buf_read[1001], buf_write[64];

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
//END COPY

void fatal_error(void) {
    write(2, "Fatal error\n", 12);
    free(msg);
    for (int fd = 0; fd < FD_SETSIZE; fd++) 
    {
        free(in[fd]);
        free(out[fd]);
        if (FD_ISSET(fd, &afds))
            close(fd);
    }
    exit(1);
}

void add_fd(int fd) {
    if (fd < 0 || fd >= FD_SETSIZE) {
        if (fd >= 0)
            close(fd);
        fatal_error();
    }
    FD_SET(fd, &afds);
}

/* Conserver l'ancien pointeur si str_join echoue. */
void append(char **buf, char *text) {
    char *newbuf = str_join(*buf, text);
    if (!newbuf)
        fatal_error();
    *buf = newbuf;
}

void notify_other(int author, char *text) {
    for (int fd = 0; fd < FD_SETSIZE; fd++)
        if (FD_ISSET(fd, &afds) && fd != sockfd && fd != author)
            append(&out[fd], text);
}

void register_client(int fd) {
    add_fd(fd);
    ids[fd] = count++;
    sprintf(buf_write, "server: client %d just arrived\n", ids[fd]);
    notify_other(fd, buf_write);
}

void remove_client(int fd) {
    FD_CLR(fd, &afds);
    free(in[fd]);
    free(out[fd]);
    in[fd] = out[fd] = NULL;
    close(fd);
    sprintf(buf_write, "server: client %d just left\n", ids[fd]);
    notify_other(fd, buf_write);
}

/* Toute la reception : lire, conserver le morceau, extraire les lignes. */
void receive_message(int fd) {
    int n = recv(fd, buf_read, 1000, MSG_DONTWAIT);
    if (n <= 0) {
        if (n == 0)
            remove_client(fd);
        return;
    }
    buf_read[n] = '\0';
    append(&in[fd], buf_read);
    int status;
    while ((status = extract_message(&in[fd], &msg)) == 1)
    {
        sprintf(buf_write, "client %d: ", ids[fd]);
        notify_other(fd, buf_write);

        notify_other(fd, msg);

        free(msg); msg = NULL;
    }
    if (status == -1)
        fatal_error();
}

void flush_output(int fd) {
    size_t len = strlen(out[fd]);
    ssize_t n = send(fd, out[fd], len, MSG_DONTWAIT | MSG_NOSIGNAL);

    /* Linux : ne pas bloquer ni quitter sur SIGPIPE. */
    if (n <= 0)
        return;
    /* Retirer les octets envoyes, puis liberer si tout est parti. */
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
        return 1;
    }
    FD_ZERO(&afds); // AJOUTER avant le bloc recopie : initialiser l'ensemble.
    /* START COPY-PASTE FROM MAIN : copier des declarations jusqu'a listen.
     * Le bloc ci-dessous montre directement les suppressions et adaptations.
     * Ne pas recopier l'accept final du main.c : il doit attendre select. */

    int connfd; // MODIFIER : retirer sockfd (global) et len (adresse client inutile).
    struct sockaddr_in servaddr; // MODIFIER : retirer ", cli" de la declaration.

    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        // printf("socket creation failed...\n"); // SUPPRIMER.
        // exit(0); // REMPLACER par fatal_error().
        fatal_error();
    }
    // else                                      // SUPPRIMER ce else et son printf.
    //     printf("Socket successfully created..\n");
    add_fd(sockfd); // AJOUTER : verifier le fd et l'inscrire dans afds.
    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
    servaddr.sin_port = htons(atoi(av[1])); // MODIFIER : atoi(av[1]) remplace 8081.

    // Binding newly created socket to given IP and verification
    if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) {
        // printf("socket bind failed...\n"); // SUPPRIMER.
        // exit(0); // REMPLACER par fatal_error().
        fatal_error();
    }
    // else                                      // SUPPRIMER ce else et son printf.
    //     printf("Socket successfully binded..\n");
    if (listen(sockfd, 10) != 0) {
        // printf("cannot listen\n"); // SUPPRIMER.
        // exit(0); // REMPLACER par fatal_error().
        fatal_error();
    }
    /* END COPY-PASTE FROM MAIN */

    while (1) {
        rfds = afds;

        FD_ZERO(&wfds);
        for (int fd = 0; fd < FD_SETSIZE; fd++)
            if (out[fd])
                FD_SET(fd, &wfds);
                
        if (select(FD_SETSIZE, &rfds, &wfds, NULL, NULL) < 0)
            fatal_error();

        if (FD_ISSET(sockfd, &rfds)) {
            connfd = accept(sockfd, NULL, NULL);
            if (connfd >= 0)
                register_client(connfd);
        }
        for (int fd = 0; fd < FD_SETSIZE; fd++) {
            if (fd != sockfd && FD_ISSET(fd, &rfds))
                receive_message(fd);
            /* Une lecture peut avoir ferme ce client : verifier aussi out. */
            if (FD_ISSET(fd, &wfds) && out[fd])
                flush_output(fd);
        }
    }
}

// rfds → recv → in[5] → extraction d’une ligne → out[4] → socket 4 prête dans wfds → send
