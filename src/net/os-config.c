#include "os-config.h"
#include <unistd.h>

// WINDOWS

#ifdef WIN32

uint32_t htobe32(uint32_t u) {
	return (u >> 24) | ((u >> 8) & 0xff00) | ((u << 8) & 0xff0000) | (u << 24);
}

uint64_t htobe64(uint64_t u) {
	return ((uint64_t) htobe32((uint32_t) u) << 32) | htobe32((uint32_t)(u >> 32));
}

uint16_t htobe16(uint16_t u) {
	return (u >> 8) | ((u & 0xff) << 8);
}

uint32_t be32toh(uint32_t u) {
	return (u << 24) | ((u << 8) & 0xff0000) | ((u >> 8) & 0xff00) | (u >> 24);
}

uint64_t be64toh(uint64_t u) {
	return ((uint64_t) be32toh((uint32_t) u) << 32) | be32toh((uint32_t)(u >> 32));
}

uint16_t be16toh(uint16_t u) {
	return (u >> 8) | (u & 0xff) << 8;
}

bool pollfd(int fd, int timeout_ms, bool in) {
	fd_set fds;
	struct timeval tv;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = 1000 * (timeout_ms % 1000);

	if(in) {
		return (select(fd + 1, &fds, NULL, NULL, &tv) > 0);
	}

	return (select(fd + 1, NULL, &fds, NULL, &tv) > 0);
}

// LINUX / OTHER

#else

#include <poll.h>

bool pollfd(int fd, int timeout_ms, bool in) {
	struct pollfd p;
	p.fd = fd;
	p.events = in ? POLLIN : POLLOUT;
	p.revents = 0;

	return (::poll(&p, 1, timeout_ms) > 0);
}
#endif

// GENERAL

bool setsock_nonblock(int fd, bool nonblock) {
#ifdef WIN32
	u_long sval = nonblock;
	return (ioctlsocket(fd, FIONBIO, &sval) == 0);
#else
	return (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) != -1);
#endif
}

int socketread(int fd, uint8_t* data, int datalen, int timeout_ms) {
        int read = 0;

        while(read < datalen) {
                if(pollfd(fd, timeout_ms, true) == 0) {
                        return ETIMEDOUT;
                }

                int rc = recv(fd, (char*)(data + read), datalen - read, MSG_DONTWAIT);

                if(rc == -1 && sockerror() == ENOTSOCK) {
                        rc = ::read(fd, data + read, datalen - read);
                }

                if(rc == 0) {
                        return ECONNRESET;
                }
                else if(rc == -1) {
                        if(sockerror() == SEWOULDBLOCK) {
                                continue;
                        }

                        return sockerror();
                }

                read += rc;
        }

        return 0;
}

