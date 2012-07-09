#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ev.h>
#include "header.h"

#define LISTEN_BACKLOG 4096
#define SOCKET_TIMEOUT 60.0

struct ev_loop *loop;

typedef struct echo_server
	echo_server_t;

struct echo_server
{
	struct sockaddr_in addr;
	ev_io wev_accept;
};

typedef struct echo_client
	echo_client_t;

struct echo_client
{
	struct sockaddr_in addr;

	ev_io wev_recv;
	/* ev_io wev_send; */
	ev_timer wev_timeout;
};

int echo_client_add(echo_server_t *s, int sd, struct sockaddr_in *addr);
int echo_client_del(echo_client_t *c);

/* --- */

void echo_client_wcb_timeout(EV_P_ ev_timer *w, int tev)
{
	echo_client_t *c = aux_memberof(echo_client_t, wev_timeout, w);
	echo_client_del(c);
}

#if 0
#define OUTPUT "HTTP/1.1 200 OK\r\n" \
	"Content-Length: 0\r\n" \
	"Connection: close\r\n\r\n"

void echo_client_wcb_send(EV_P_ ev_io *w, int tev)
{
	int nb;
	echo_client_t *c = aux_memberof(echo_client_t, wev_send, w);

	/* nb = aux_unix_send(w->fd, buf, nb); */
	nb = aux_unix_send(w->fd, OUTPUT, sizeof(OUTPUT) - 1);

	if (0 > nb)
	{
		echo_client_del(c);
		return;
	}

	echo_client_del(c);
}
#endif

void echo_client_wcb_recv(EV_P_ ev_io *w, int tev)
{
	int nb;
	char buf [4096];
	echo_client_t *c = aux_memberof(echo_client_t, wev_recv, w);

	nb = aux_unix_recv(w->fd, buf, 4096);
	/* fprintf(stderr, "recv %d bytes (%d: %s) %.*s\n", nb, errno, strerror(errno), nb, buf); */

	if (0 > nb)
	{
		if (EAGAIN == errno)
		{
			ev_timer_again(loop, &c->wev_timeout);
			return;
		}

		fprintf(stderr, "recv error fd=%d (%d: %s)\n", w->fd, errno, strerror(errno));
		echo_client_del(c);
		return;
	}

	if (0 == nb)
	{
		fprintf(stderr, "recv done fd=%d\n", w->fd);
		echo_client_del(c);
		return;
	}

#if 0
	ev_io_stop(loop, &c->wev_recv);
	ev_io_start(loop, &c->wev_send);
#endif

	nb = aux_unix_send(w->fd, buf, nb);
	/* nb = aux_unix_send(w->fd, OUTPUT, sizeof(OUTPUT) - 1); */
	/* fprintf(stderr, "sent %d bytes (%d: %s)\n", nb, errno, strerror(errno)); */

	if (0 > nb)
	{
		fprintf(stderr, "send error fd=%d (%d: %s)\n", w->fd, errno, strerror(errno));
		echo_client_del(c);
		return;
	}

	/* echo_client_del(c); */

	ev_timer_again(loop, &c->wev_timeout);
}

int echo_client_add(echo_server_t *s, int sd, struct sockaddr_in *addr)
{
	echo_client_t *c = malloc(sizeof(*c));
	if (NULL == c) return -1;

	c->addr.sin_family = addr->sin_family;
	c->addr.sin_addr.s_addr = addr->sin_addr.s_addr;
	c->addr.sin_port = addr->sin_port;

	ev_io_init(&c->wev_recv, echo_client_wcb_recv, sd, EV_READ);
	/* ev_io_init(&c->wev_send, echo_client_wcb_send, sd, EV_WRITE); */
	ev_timer_init(&c->wev_timeout, echo_client_wcb_timeout, 0, SOCKET_TIMEOUT);
	ev_timer_again(loop, &c->wev_timeout);
	ev_io_start(loop, &c->wev_recv);

	return 0;
}

int echo_client_del(echo_client_t *c)
{
	ev_io_stop(loop, &c->wev_recv);
	/* ev_io_stop(loop, &c->wev_send); */
	ev_timer_stop(loop, &c->wev_timeout);

	close(c->wev_recv.fd);

	free(c);

	return 0;
}

void echo_server_wcb_accept(EV_P_ ev_io *w, int tev)
{
	int sd, rc;
	struct sockaddr_in addr;
	socklen_t addrlen;

	echo_server_t *s = aux_memberof(echo_server_t, wev_accept, w);

	if (EV_ERROR & tev)
	{
		/* echo_server_enough(s); */
		return;
	}

	addrlen = sizeof(addr);

	if (0 > (sd = accept(w->fd, (struct sockaddr *) &addr, &addrlen)))
	{
		return;
	}

	if (0 > (rc = aux_set_nonblk(sd, 1)))
	{
		return;
	}

	echo_client_add(s, sd, &addr);
}

int echo_server_listen(echo_server_t *s, const char *host, const char *port)
{
	int sd, rc;

	sd = socket(AF_INET, SOCK_STREAM, 0);
	if (0 > sd) return -1;

	rc = aux_set_nonblk(sd, 1);
	if (0 > rc) return -1;

	rc = aux_set_sckopt(sd, SOL_SOCKET, SO_REUSEADDR, 1);
	if (0 > rc) return -1;

	memset(s, 0, sizeof(*s));

	s->addr.sin_family = AF_INET;
	s->addr.sin_addr.s_addr = inet_addr(host);
	s->addr.sin_port = htons(atoi(port));

	rc = bind(sd, (struct sockaddr *) &s->addr, sizeof(s->addr));
	if (0 > rc) return -1;

	rc = listen(sd, LISTEN_BACKLOG);
	if (0 > rc) return -1;

	ev_io_init(&s->wev_accept, echo_server_wcb_accept, sd, EV_READ);
	ev_io_start(loop, &s->wev_accept);

	return 0;
}

int main(int argc, char **argv)
{
	int rc;

	if (3 > argc)
	{
		fprintf(stderr, "Usage: %s host port\n", argv[0]);
		return -1;
	}

	loop = ev_default_loop(0);

	echo_server_t s;

	rc = echo_server_listen(&s, argv[1], argv[2]);
	if (0 > rc) return -1;

	ev_run(loop, 0);

	return 0;
}

