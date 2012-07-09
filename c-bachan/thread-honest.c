#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ev.h>
#include <pthread.h>
#include "header.h"

#define LISTEN_BACKLOG 4096
#define SOCKET_TIMEOUT 60.0

__thread struct ev_loop *loop;
pthread_mutex_t accept_mtx = PTHREAD_MUTEX_INITIALIZER;

typedef struct echo_server
	echo_server_t;

struct echo_server
{
	ev_io wev_accept;
};

typedef struct echo_client
	echo_client_t;

struct echo_client
{
	struct sockaddr_in addr;

	ev_io wev_recv;
	ev_io wev_send;
	ev_timer wev_timeout;

#define TO_SEND_SIZE 4096 * 4
	char to_send [TO_SEND_SIZE];
	char *to_send_beg;
	size_t to_send_size;
};

int echo_client_add(echo_server_t *s, int sd, struct sockaddr_in *addr);
int echo_client_del(echo_client_t *c);

/* --- */

void echo_client_wcb_timeout(EV_P_ ev_timer *w, int tev)
{
	echo_client_t *c = aux_memberof(echo_client_t, wev_timeout, w);
	echo_client_del(c);
}

void echo_client_wcb_send(EV_P_ ev_io *w, int tev)
{
	int sent_nb;
	echo_client_t *c = aux_memberof(echo_client_t, wev_send, w);

	sent_nb = aux_unix_send(w->fd, c->to_send_beg, c->to_send_size);
	/* sent_nb = aux_unix_send(w->fd, OUTPUT, sizeof(OUTPUT) - 1); */
	/* fprintf(stderr, "sent %d bytes (%d: %s)\n", sent_nb, errno, strerror(errno)); */

	if (0 > sent_nb)
	{
		if (EAGAIN == errno)
		{
			return;
		}

		fprintf(stderr, "send error fd=%d (%d: %s)\n", w->fd, errno, strerror(errno));
		echo_client_del(c);
		return;
	}

	c->to_send_beg += sent_nb;
	c->to_send_size -= sent_nb;

	if (c->to_send_size > 0)
	{
		return;
	}

	ev_io_stop(loop, &c->wev_send);
}

void echo_client_wcb_recv(EV_P_ ev_io *w, int tev)
{
	int nb;
	echo_client_t *c = aux_memberof(echo_client_t, wev_recv, w);

	int rem_size = c->to_send + TO_SEND_SIZE - (c->to_send_beg + c->to_send_size);

	if (rem_size < 32)
	{
		/* fprintf(stderr, "rem_size=%d, c->to_send_size=%u, fd=%d\n", rem_size, (unsigned) c->to_send_size, w->fd); */
		memcpy(c->to_send, c->to_send_beg, c->to_send_size);

		c->to_send_beg = c->to_send;
		rem_size = TO_SEND_SIZE - c->to_send_size;

		/* fprintf(stderr, "rem_size=%d\n", rem_size); */
	}

	nb = aux_unix_recv(w->fd, c->to_send_beg + c->to_send_size, rem_size);
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
		/* fprintf(stderr, "recv done fd=%d\n", w->fd); */
		/* echo_client_del(c); */
		return;
	}

	c->to_send_size += nb;

	int sent_nb;

	sent_nb = aux_unix_send(w->fd, c->to_send_beg, c->to_send_size);
	/* nb = aux_unix_send(w->fd, OUTPUT, sizeof(OUTPUT) - 1); */
	/* fprintf(stderr, "sent %d bytes (%d: %s)\n", nb, errno, strerror(errno)); */

	if (0 > sent_nb)
	{
		if (EAGAIN == errno)
		{
			ev_io_start(loop, &c->wev_send);
			return;
		}

		fprintf(stderr, "send error fd=%d (%d: %s)\n", w->fd, errno, strerror(errno));
		echo_client_del(c);
		return;
	}

	c->to_send_beg += sent_nb;
	c->to_send_size -= sent_nb;

	if (c->to_send_size > 0)
	{
		ev_io_start(loop, &c->wev_send);
		return;
	}

#if 0
	nb = aux_unix_send(w->fd, buf, nb);
	/* fprintf(stderr, "sent %d bytes (%d: %s)\n", nb, errno, strerror(errno)); */

	if (0 > nb)
	{
		fprintf(stderr, "send error fd=%d (%d: %s)\n", w->fd, errno, strerror(errno));
		echo_client_del(c);
		return;
	}

	ev_timer_again(loop, &c->wev_timeout);
#endif
}

int echo_client_add(echo_server_t *s, int sd, struct sockaddr_in *addr)
{
	echo_client_t *c = malloc(sizeof(*c));
	if (NULL == c) return -1;

	c->addr.sin_family = addr->sin_family;
	c->addr.sin_addr.s_addr = addr->sin_addr.s_addr;
	c->addr.sin_port = addr->sin_port;

	ev_io_init(&c->wev_recv, echo_client_wcb_recv, sd, EV_READ);
	ev_io_init(&c->wev_send, echo_client_wcb_send, sd, EV_WRITE);
	ev_timer_init(&c->wev_timeout, echo_client_wcb_timeout, 0, SOCKET_TIMEOUT);
	ev_timer_again(loop, &c->wev_timeout);
	ev_io_start(loop, &c->wev_recv);

	c->to_send_beg = c->to_send;
	c->to_send_size = 0;

	return 0;
}

int echo_client_del(echo_client_t *c)
{
	ev_io_stop(loop, &c->wev_recv);
	ev_io_stop(loop, &c->wev_send);
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

	pthread_mutex_lock(&accept_mtx);
	sd = accept(w->fd, (struct sockaddr *) &addr, &addrlen);
	pthread_mutex_unlock(&accept_mtx);

/* fprintf(stderr, "accept in thread=%d fd=%d (%d: %s)\n", (int) pthread_self(), sd, errno, strerror(errno)); */

	if (0 > sd)
	{
		return;
	}

	if (0 > (rc = aux_set_nonblk(sd, 1)))
	{
		return;
	}

#if 0
	if (0 > (rc = aux_set_sckopt(sd, IPPROTO_TCP, TCP_NODELAY, 1)))
	{
		fprintf(stderr, "sockopt TCP_NODELAY error\n");
	}
#endif

	echo_client_add(s, sd, &addr);
}

int echo_socket_listen(const char *host, const char *port)
{
	int sd, rc;
	struct sockaddr_in addr;

	sd = socket(AF_INET, SOCK_STREAM, 0);
	if (0 > sd) return -1;

	rc = aux_set_nonblk(sd, 1);
	if (0 > rc) return -1;

	rc = aux_set_sckopt(sd, SOL_SOCKET, SO_REUSEADDR, 1);
	if (0 > rc) return -1;

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(host);
	addr.sin_port = htons(atoi(port));

	rc = bind(sd, (struct sockaddr *) &addr, sizeof(addr));
	if (0 > rc) return -1;

	rc = listen(sd, LISTEN_BACKLOG);
	if (0 > rc) return -1;

	return sd;
}

void *thread_func(void *arg)
{
	long sd = (long) arg;

	loop = ev_loop_new(0);

	echo_server_t s;

	ev_io_init(&s.wev_accept, echo_server_wcb_accept, sd, EV_READ);
	ev_io_start(loop, &s.wev_accept);

	ev_run(loop, 0);

	return NULL;
}

int main(int argc, char **argv)
{
	long sd;

	if (3 > argc)
	{
		fprintf(stderr, "Usage: %s host port\n", argv[0]);
		return -1;
	}

	sd = echo_socket_listen(argv[1], argv[2]);
	if (0 > sd) return -1;

	/* --- */

	int num_cores = (4 == argc) ? atoi(argv[3]) : sysconf(_SC_NPROCESSORS_ONLN);
	fprintf(stderr, "found %d cores\n", num_cores);

	int rc, i;

	for (i = 0; i < num_cores - 1; ++i)
	{
		pthread_t thread_id;

		rc = pthread_create(&thread_id, NULL, thread_func, (void *) sd);

		if (0 != rc)
		{
			fprintf(stderr, "thread create error %d\n", rc);
			return -1;
		}
	}

	thread_func((void *) sd);

	return 0;
}

