#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ev.h>
#include "header.h"

#define SILENT_TIMEOUT 0.001
#define MAX_CLIENTS 10000
#define RESEND_INTERVAL 0.01
#define EXECUTION_TIME 60.0

/* --- */

struct ev_loop *loop;
ev_timer wev_exit;
ev_timer wev_silent;
struct sockaddr_in addr;
int number_of_clients;

typedef struct client
	client_t;

struct client
{
	ev_io wev_recv;
	ev_io wev_send;
	ev_io wev_connect;
	ev_timer wev_timeout;

	ev_tstamp ts;

	unsigned to_send;
};

double times_sum;
size_t times_count;
double times_max;
double times_min;

/* --- */

#define REQUEST "123"

int client_add();
int client_del(client_t *c);
int client_send(client_t *c);

void wcb_timeout(EV_P_ ev_timer *w, int tev)
{
	client_t *c = aux_memberof(client_t, wev_timeout, w);

	c->to_send++;

	ev_io_start(loop, &c->wev_send);

#if 0
	int rc = client_send(c);
	if (0 > rc) return;
#endif
}

void wcb_connect(EV_P_ ev_io *w, int tev)
{
	client_t *c = aux_memberof(client_t, wev_connect, w);

	if (EV_READ & tev)
	{
		fprintf(stderr, "conn error fd=%d\n", w->fd);
		client_del(c);
		return;
	}

#if 0
	int rc = client_send(c);
	if (0 > rc) return;
#endif

	c->to_send = 1;

	ev_io_stop(loop, &c->wev_connect);
	ev_io_start(loop, &c->wev_send);
	ev_io_start(loop, &c->wev_recv);
}

void wcb_send(EV_P_ ev_io *w, int tev)
{
	client_t *c = aux_memberof(client_t, wev_send, w);

	for (; c->to_send; c->to_send--)
	{
		int rc = client_send(c);
		if (0 > rc) return;
	}

	ev_io_stop(loop, &c->wev_send);
}

void wcb_recv(EV_P_ ev_io *w, int tev)
{
	client_t *c = aux_memberof(client_t, wev_recv, w);

	char buf [4096];

	int nb = aux_unix_recv(w->fd, buf, 4096);

	double time = ev_now(loop) - c->ts;
	times_sum += time;
	times_count++;
	if (times_max < time) times_max = time;
	if (times_min > time) times_min = time;

	/* fprintf(stderr, "recv fd=%d %f (%d bytes) %.*s\n", w->fd, ev_now(loop) - c->ts, nb, nb, buf); */
	/* if (nb != 57) fprintf(stderr, "recv error %d bytes\n", nb); */

	if (0 > nb)
	{
		if (errno == EAGAIN)
		{
			return;
		}

		fprintf(stderr, "recv error fd=%d (%d: %s)\n", w->fd, errno, strerror(errno));
		client_del(c);
		return;
	}
}

int client_send(client_t *c)
{
	int nb = aux_unix_send(c->wev_recv.fd, REQUEST, sizeof(REQUEST) - 1);

	c->ts = ev_now(loop);

	/* fprintf(stderr, "send fd=%d\n", c->wev_recv.fd); */

	if (0 > nb)
	{
		fprintf(stderr, "send error fd=%d (%d: %s)\n", c->wev_recv.fd, errno, strerror(errno));
		client_del(c);
		return -1;
	}

	return 0;
}

int client_add()
{
	int sd, rc;

	client_t *c;
	
	if (NULL == (c = calloc(1, sizeof(*c))))
	{
		return -1;
	}

	if (0 > (sd = socket(AF_INET, SOCK_STREAM, 0)))
	{
		fprintf(stderr, "sock error\n");
		return -1;
	}

	if (0 > (rc = aux_set_nonblk(sd, 1)))
	{
		close(sd);
		return -1;
	}

	rc = connect(sd, (struct sockaddr *) &addr, sizeof(addr));

	if (0 > rc && EINPROGRESS != errno)
	{
		fprintf(stderr, "conn error fd=%d\n", sd);
		close(sd);
		return -1;
	}

	ev_io_init(&c->wev_recv, wcb_recv, sd, EV_READ);
	ev_io_init(&c->wev_send, wcb_send, sd, EV_WRITE);
	ev_io_init(&c->wev_connect, wcb_connect, sd, EV_READ | EV_WRITE);
	ev_timer_init(&c->wev_timeout, wcb_timeout, 0, RESEND_INTERVAL);
	ev_timer_again(loop, &c->wev_timeout);
	ev_io_start(loop, &c->wev_connect);

	return 0;
}

int client_del(client_t *c)
{
fprintf(stderr, "del client to_send=%d\n", c->to_send);

	ev_io_stop(loop, &c->wev_recv);
	ev_io_stop(loop, &c->wev_send);
	ev_io_stop(loop, &c->wev_connect);
	ev_timer_stop(loop, &c->wev_timeout);

	close(c->wev_recv.fd);

	free(c);

	return 0;
}

void wcb_exit(EV_P_ ev_timer *w, int tev)
{
	ev_break(EV_A_ EVBREAK_ALL);
}

void wcb_silent(EV_P_ ev_timer *w, int tev)
{
	if (number_of_clients++ >= MAX_CLIENTS)
	{
		ev_timer_stop(EV_A_ w);
		return;
	}

	client_add();
}

int main(int argc, char **argv)
{
	if (3 > argc)
	{
		fprintf(stderr, "Usage: %s host port\n", argv[0]);
		return -1;
	}

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(argv[1]);
	addr.sin_port = htons(atoi(argv[2]));

	number_of_clients = 0;

	times_sum = 0.0;
	times_count = 0;
	times_max = 0.0;
	times_min = 100.0;

	loop = ev_default_loop(0);

	ev_timer_init(&wev_exit, wcb_exit, 0, EXECUTION_TIME);
	ev_timer_again(loop, &wev_exit);

	ev_timer_init(&wev_silent, wcb_silent, 0, SILENT_TIMEOUT);
	ev_timer_again(loop, &wev_silent);

	ev_run(loop, 0);

	fprintf(stderr, "times_sum=%f times_count=%u avg=%f min=%f max=%f\n", times_sum, (unsigned) times_count, times_sum / times_count, times_min, times_max);

	return 0;
}

