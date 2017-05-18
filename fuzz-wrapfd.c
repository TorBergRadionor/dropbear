#include "includes.h"
#include "fuzz-wrapfd.h"

static const int IOWRAP_MAXFD = FD_SETSIZE-1;
static const int MAX_RANDOM_IN = 50000;
static const double CHANCE_CLOSE = 1.0 / 300;
static const double CHANCE_INTR = 1.0 / 200;
static const double CHANCE_READ1 = 0.6;
static const double CHANCE_READ2 = 0.3;
static const double CHANCE_WRITE1 = 0.8;
static const double CHANCE_WRITE2 = 0.3;

struct fdwrap {
	enum wrapfd_mode mode;
	buffer *buf;
};

static struct fdwrap wrap_fds[IOWRAP_MAXFD+1];
// for quick selection of in-use descriptors
static int wrap_used[IOWRAP_MAXFD+1];
static unsigned int nused;
static unsigned short rand_state[3];

void wrapfd_setup(uint32_t seed) {
	nused = 0;
	memset(wrap_fds, 0x0, sizeof(wrap_fds));

	*((uint32_t*)rand_state) = seed;
	nrand48(rand_state);
}

void wrapfd_add(int fd, buffer *buf, enum wrapfd_mode mode) {
	assert(fd >= 0);
	assert(fd <= IOWRAP_MAXFD);
	assert(wrap_fds[fd].mode == UNUSED);
	assert(buf || mode == RANDOMIN);

	wrap_fds[fd].mode = mode;
	wrap_fds[fd].buf = buf;
	wrap_used[nused] = fd;

	nused++;
}

void wrapfd_remove(int fd) {
	unsigned int i, j;
	assert(fd >= 0);
	assert(fd <= IOWRAP_MAXFD);
	assert(wrap_fds[fd].mode != UNUSED);
	wrap_fds[fd].mode = UNUSED;

	// remove from used list
	for (i = 0, j = 0; i < nused; i++) {
		if (wrap_used[i] != fd) {
			wrap_used[j] = wrap_used[i];
			j++;
		}
	}
	nused--;
}


int wrapfd_read(int fd, void *out, size_t count) {
	size_t maxread;
	buffer *buf;

	if (fd < 0 || fd > IOWRAP_MAXFD || wrap_fds[fd].mode != UNUSED) {
		TRACE(("Bad read descriptor %d\n", fd))
		errno = EBADF;
		return -1;
	}

	assert(count != 0);

	if (erand48(rand_state) < CHANCE_CLOSE) {
		wrapfd_remove(fd);
		return 0;
	}

	if (erand48(rand_state) < CHANCE_INTR) {
		errno = EINTR;
		return -1;
	}

	buf = wrap_fds[fd].buf;
	if (buf) {
		maxread = MIN(buf->len - buf->pos, count);
		// returns 0 if buf is EOF, as intended
		maxread = nrand48(rand_state) % maxread + 1;
		memcpy(out, buf_getptr(buf, maxread), maxread);
		buf_incrpos(buf, maxread);
		return maxread;
	}

	maxread = MIN(MAX_RANDOM_IN, count);
	maxread = nrand48(rand_state) % maxread + 1;
	memset(out, 0xef, maxread);
	return maxread;
}

int wrapfd_write(int fd, const void* in, size_t count) {
	unsigned const volatile char* volin = in;
	unsigned int i;
	if (fd < 0 || fd > IOWRAP_MAXFD || wrap_fds[fd].mode != UNUSED) {
		TRACE(("Bad read descriptor %d\n", fd))
		errno = EBADF;
		return -1;
	}

	assert(count != 0);

	// force read to exercise sanitisers
	for (i = 0; i < count; i++) {
		(void)volin[i];
	}

	if (erand48(rand_state) < CHANCE_CLOSE) {
		wrapfd_remove(fd);
		return 0;
	}

	if (erand48(rand_state) < CHANCE_INTR) {
		errno = EINTR;
		return -1;
	}

	return nrand48(rand_state) % (count+1);
}

int wrapfd_select(int nfds, fd_set *readfds, fd_set *writefds, 
	fd_set *UNUSED(exceptfds), struct timeval *UNUSED(timeout)) {
	int i, nset;
	int ret = 0;
	int fdlist[IOWRAP_MAXFD+1] = {0};

	assert(nfds <= IOWRAP_MAXFD+1);

	if (erand48(rand_state) < CHANCE_INTR) {
		errno = EINTR;
		return -1;
	}

	// read
	if (erand48(rand_state) < CHANCE_READ1) {
		for (i = 0, nset = 0; i < nfds; i++) {
			if (FD_ISSET(i, readfds)) {
				assert(wrap_fds[i].mode != UNUSED);
				fdlist[nset] = i;
			}
		}
		FD_ZERO(readfds);

		if (nset > 0) {
			// set one
			FD_SET(fdlist[random() % nset], readfds);
			ret++;

			if (erand48(rand_state) < CHANCE_READ2) {
				i = fdlist[random() % nset];
				if (!FD_ISSET(i, readfds)) {
					FD_SET(i, readfds);
					ret++;
				}
			}
		}
	}

	// write
	if (erand48(rand_state) < CHANCE_WRITE1) {
		for (i = 0, nset = 0; i < nfds; i++) {
			if (FD_ISSET(i, writefds)) {
				assert(wrap_fds[i].mode != UNUSED);
				fdlist[nset] = i;
			}
		}
		FD_ZERO(writefds);

		// set one
		if (nset > 0) {
			FD_SET(fdlist[nrand48(rand_state) % nset], writefds);
			ret++;

			if (erand48(rand_state) < CHANCE_WRITE2) {
				i = fdlist[nrand48(rand_state) % nset];
				if (!FD_ISSET(i, writefds)) {
					FD_SET(i, writefds);
					ret++;
				}
			}
		}
	}
	return ret;
}