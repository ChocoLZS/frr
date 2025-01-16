#ifndef _BGPMGMT_H
#define _BGPMGMT_H

#include <netinet/in.h>

#include <stdbool.h>

#include "lib/libfrr.h"
#include "lib/frrevent.h"
#include "lib/memory.h"
#include "lib/stream.h"


DECLARE_MGROUP(BGPMGMT);
TAILQ_HEAD(socket_queue, bgpmgmt_socket_ctx);

struct _bgpmgmt_global {
    int bgpmgmt_sock;
    struct event *bgpmgmt_sockev;

    struct socket_queue bgpmgmt_sockq;
};

extern struct _bgpmgmt_global bgpmgmt_global;

/* Export callback functions for `event.c`. */
extern struct event_loop *master;

/*
 * bgpmgmtd.c
 */

/**
 * 创建新的socket服务
 */
int mgmt_socket_init(const struct sockaddr *sa, socklen_t salen, bool client);

#endif /* _BGPMGMT_H */