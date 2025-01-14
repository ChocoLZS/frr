// SPDX-License-Identifier: GPL-2.0-or-later
/*********************************************************************
 * Socket server side implementation for logging.
 */

// #include "bgpmgmt.h"
#include "side_socket.h"

DEFINE_MGROUP(SOCKET, "Management socket");
DEFINE_MTYPE_STATIC(SOCKET, SIDE_SOCKET_CTX, "Management socket context");

static void side_socket_read(struct event *t);
static void side_socket_accept(struct event *t);

/**
 * Accept new client connection and handle data reading.
 */
static void side_socket_accept(struct event *t)
{
    struct side_socket_ctx *ctx = EVENT_ARG(t);
    int client_sock;

    /* Accept new connection */
    client_sock = accept(ctx->sock, NULL, 0);
    if (client_sock == -1) {
        zlog_warn("%s: accept failed: %s", __func__, strerror(errno));
        goto reschedule_and_return;
    }

    /* Set non blocking socket */
    set_nonblocking(client_sock);

    /* Store client socket */
    ctx->client_sock = client_sock;

    /* Start reading data */
    event_add_read(master, side_socket_read, ctx, client_sock, &ctx->read_ev);

    zlog_info("%s: new client connected on socket %d", __func__, client_sock);
    return;

reschedule_and_return:
    event_add_read(master, side_socket_accept, ctx, ctx->sock, &ctx->read_ev);
}

/**
 * Read data from socket and log to stdout.
 */
static void side_socket_read(struct event *t)
{
    struct side_socket_ctx *ctx = EVENT_ARG(t);
    ssize_t rv;
    char *buf;
    size_t already_read;

    /* Check if buffer has enough space */
    if (ctx->inbuf->endp == ctx->inbuf->size) {
        /* Buffer full - reorganize */
        stream_pulldown(ctx->inbuf);
        /* Still full? Drop data */
        if (ctx->inbuf->endp == ctx->inbuf->size) {
            zlog_warn("%s: buffer full, dropping data", __func__);
            goto reschedule;
        }
    }

    /* Read data from socket */
    rv = stream_read_try(ctx->inbuf, ctx->client_sock,
                        STREAM_WRITEABLE(ctx->inbuf));

    if (rv == 0) {
        /* Connection closed */
        zlog_info("%s: client disconnected", __func__);
        close(ctx->client_sock);
        ctx->client_sock = -1;
        stream_reset(ctx->inbuf);
        /* Go back to accept mode */
        event_add_read(master, side_socket_accept, ctx, ctx->sock, &ctx->read_ev);
        return;
    }

    if (rv == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            zlog_warn("%s: socket read failed: %s", __func__, strerror(errno));
            close(ctx->client_sock);
            ctx->client_sock = -1;
            stream_reset(ctx->inbuf);
            /* Go back to accept mode */
            event_add_read(master, side_socket_accept, ctx, ctx->sock, &ctx->read_ev);
            return;
        }
        goto reschedule;
    }

    /* Get buffer content */
    buf = (char *)stream_pnt(ctx->inbuf);
    already_read = STREAM_READABLE(ctx->inbuf);

    /* Log received data */
    if (already_read > 0) {
        char log_buf[SIDE_SOCKET_BUF_SIZE + 1];
        size_t to_copy = already_read > SIDE_SOCKET_BUF_SIZE ? 
                        SIDE_SOCKET_BUF_SIZE : already_read;

        memcpy(log_buf, buf, to_copy);
        log_buf[to_copy] = '\0';
        zlog_info("Received data: %s", log_buf);

        /* Move read pointer forward */
        stream_forward_getp(ctx->inbuf, already_read);
    }

    /* Clean up buffer */
    stream_pulldown(ctx->inbuf);

reschedule:
    event_add_read(master, side_socket_read, ctx, ctx->client_sock, &ctx->read_ev);
}

/**
 * Initialize side socket server.
 */
int side_socket_init(const struct sockaddr *sa, socklen_t salen)
{
    struct side_socket_ctx *ctx;
    int sock;

    // hook_register(frr_fini, side_socket_clean);

    /* Create socket */
    sock = socket(sa->sa_family, SOCK_STREAM, 0);
    if (sock == -1) {
        zlog_warn("%s: failed to create socket: %s", __func__, strerror(errno));
        return -1;
    }

    /* Set socket options */
    sockopt_reuseaddr(sock);
    set_nonblocking(sock);

    /* Handle UNIX socket special case */
    if (sa->sa_family == AF_UNIX)
        unlink(((struct sockaddr_un *)sa)->sun_path);

    /* Bind socket */
    if (bind(sock, sa, salen) == -1) {
        zlog_warn("%s: failed to bind socket: %s", __func__, strerror(errno));
        close(sock);
        return -1;
    }

    /* Listen for connections */
    if (listen(sock, SOMAXCONN) == -1) {
        zlog_warn("%s: failed to listen on socket: %s", __func__, strerror(errno));
        close(sock);
        return -1;
    }

    /* Allocate context */
    ctx = XCALLOC(MTYPE_SIDE_SOCKET_CTX, sizeof(*ctx));
    ctx->sock = sock;
    ctx->client_sock = -1;
    ctx->inbuf = stream_new(SIDE_SOCKET_BUF_SIZE);

    /* Start accepting connections */
    event_add_read(master, side_socket_accept, ctx, sock, &ctx->read_ev);

    zlog_notice("%s: side socket server initialized on socket %d", __func__, sock);
    return 0;
}



/**
 * Clean up side socket resources.
 */
void side_socket_clean(struct side_socket_ctx *ctx)
{
    if (!ctx)
        return;

    /* Close sockets */
    if (ctx->client_sock != -1)
        close(ctx->client_sock);
    if (ctx->sock != -1)
        close(ctx->sock);

    /* Free resources */
    EVENT_OFF(ctx->read_ev);
    stream_free(ctx->inbuf);
    XFREE(MTYPE_SIDE_SOCKET_CTX, ctx);
}