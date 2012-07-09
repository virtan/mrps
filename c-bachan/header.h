#ifndef __HEADER_H__
#define __HEADER_H__

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#define aux_memberof(t,n,p) (((t*)(((unsigned char *)(p))-offsetof(t,n))))

static inline
int aux_set_nonblk(int s, int value)
{
	return ioctl(s, FIONBIO, &value);
}

static inline
int aux_set_sckopt(int s, int level, int key, int value)
{
	return setsockopt(s, level, key, (void *) &value, sizeof(value));
}

static inline
int aux_unix_send(int fd, void *data, size_t size)
{
	return send(fd, data, size, MSG_NOSIGNAL);
}

static inline
int aux_unix_recv(int fd, void *data, size_t size)
{
	int rc;

	for (;;)
	{
		rc = recv(fd, data, size, 0);
		if (0 <= rc) break;

		if (EINTR != errno)
		{
			return -1;
		}
	}

	return rc;
}

#endif /* __HEADER_H__ */
