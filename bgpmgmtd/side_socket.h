// SPDX-License-Identifier: GPL-2.0-or-later
/*********************************************************************
 * Socket server side implementation for logging.
 */

#ifndef _SIDE_SOCKET_H_
#define _SIDE_SOCKET_H_

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <err.h>
#include <stdbool.h>

#include "lib/libfrr.h"
#include "lib/frrevent.h"
#include "lib/memory.h"
#include "lib/stream.h"


DECLARE_MGROUP(SOCKET);

/* Data stream buffer size */
#define SIDE_SOCKET_BUF_SIZE 8192

struct side_socket_ctx {
    /* Socket file descriptor */
    int sock;
    /* Client file descriptor */
    int client_sock;
    /* Input buffer data */
    struct stream *inbuf;
    /* Input event data */
    struct event *read_ev;
};

extern struct event_loop *master;

/* Functions prototypes */
int side_socket_init(const struct sockaddr *sa, socklen_t salen);
void side_socket_clean(struct side_socket_ctx *ctx);

#endif /* _SIDE_SOCKET_H_ */