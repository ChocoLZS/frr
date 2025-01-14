#include "bgpmgmt.h"

DEFINE_MGROUP(BGPMGMT, "Management");
DEFINE_MTYPE_STATIC(BGPMGMT, BGPMGMT_SOCKET_CTX, "Management socket context");

#define BGPMGMT_SOCKET_BUF_SIZE 8192

struct bgpmgmt_socket_ctx {
    /** Client file descriptor. */
	int sock;
	/** Is this a connected or accepted? */
	bool client;
	/** Is the socket still connecting? */
	bool connecting;
	/** Client/server address. */
	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
		struct sockaddr_in6 sin6;
		struct sockaddr_un sun;
	} addr;
	/** Address length. */
	socklen_t addrlen;
	/** Data plane current last used ID. */
	uint16_t last_id;

	/** Input buffer data. */
	struct stream *inbuf;
	/** Output buffer data. */
	struct stream *outbuf;
	/** Input event data. */
	struct event *inbufev;
	/** Output event data. */
	struct event *outbufev;
	/** Connection event. */
	struct event *connectev;

	/** Amount of bytes read. */
	uint64_t in_bytes;
	/** Amount of bytes read peak. */
	uint64_t in_bytes_peak;
	/** Amount of bytes written. */
	uint64_t out_bytes;
	/** Amount of bytes written peak. */
	uint64_t out_bytes_peak;
	/** Amount of output buffer full events (`bfd_dplane_enqueue` failed).
	 */
	uint64_t out_fullev;

	/** Amount of messages read (full messages). */
	uint64_t in_msgs;
	/** Amount of messages enqueued (maybe written). */
	uint64_t out_msgs;

	TAILQ_ENTRY(bgpmgmt_socket_ctx) entry;
};

typedef void (*bgpmgmt_socket_expcet_cb)(unsigned char data[], void *arg);

/**
 * 关闭socket
 */
void socket_close(int *s);
static void bgpmgmt_socket_ctx_free(struct bgpmgmt_socket_ctx *bsc);
static int bgpmgmt_socket_expect(struct bgpmgmt_socket_ctx *bsc, uint16_t id,
				bgpmgmt_socket_expcet_cb cb, void *arg);
static void bgpmgmt_socket_read(struct event *t);
static struct bgpmgmt_socket_ctx *bgpmgmt_socket_ctx_new(int sock);
static void mgmt_socket_accept(struct event *t);
static int mgmt_socket_finish_late(void);


void socket_close(int *s)
{
	if (*s <= 0)
		return;

	if (close(*s) != 0)
		zlog_err("%s: close(%d): (%d) %s", __func__, *s, errno,
			 strerror(errno));

	*s = -1;
}

/**
 * 释放bgpmgmt_socket_ctx资源 
 */
static void bgpmgmt_socket_ctx_free(struct bgpmgmt_socket_ctx *bsc)
{
	zlog_info("%s: terminating data plane client %d", __func__,
			   bsc->sock);

// free_resources:
	/* Remove from the list of attached socket */
	TAILQ_REMOVE(&bgpmgmt_global.bgpmgmt_sockq, bsc, entry);

	socket_close(&bsc->sock);
	stream_free(bsc->inbuf);
	stream_free(bsc->outbuf);
	EVENT_OFF(bsc->inbufev);
	EVENT_OFF(bsc->outbufev);
	XFREE(MTYPE_BGPMGMT_SOCKET_CTX, bsc);
}

static int bgpmgmt_socket_expect(struct bgpmgmt_socket_ctx *bsc, uint16_t id,
				bgpmgmt_socket_expcet_cb cb, void *arg)
{
	char *data;
	size_t data_len = 0;
	ssize_t rv;

	/**
	 * 普通读取字符流并输出至stdout
	 */
	if (bsc->inbuf->endp == bsc->inbuf->size) { 
		stream_pulldown(bsc->inbuf);

		if (bsc->inbuf->endp == bsc->inbuf->size) {
			zlog_warn("%s: buffer full, dropping data", __func__);
			goto reschedule;
		}
	}

	rv = stream_read_try(bsc->inbuf, bsc->sock, 
						STREAM_WRITEABLE(bsc->inbuf));
	
	if (rv == 0) {
        /* Connection closed */
        zlog_info("%s: client disconnected", __func__);
        bgpmgmt_socket_ctx_free(bsc);
        return -1;
    }

	if (rv == -1) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			zlog_warn("%s: socket read failed: %s", __func__, strerror(errno));
			bgpmgmt_socket_ctx_free(bsc);
			return -1;
		}
		goto reschedule;
	}

	data = (char *)stream_pnt(bsc->inbuf);
	data_len = STREAM_READABLE(bsc->inbuf);

	if (data_len > 0) {
		char log_buf[BGPMGMT_SOCKET_BUF_SIZE + 1];
		size_t to_copy = data_len > BGPMGMT_SOCKET_BUF_SIZE ? 
						BGPMGMT_SOCKET_BUF_SIZE : data_len;
		memcpy(log_buf, data, to_copy);
		log_buf[to_copy] = '\0';
		zlog_info("Received data: %s", log_buf);

		stream_forward_getp(bsc->inbuf, data_len);
	}
	/* Clean up buffer */
	stream_pulldown(bsc->inbuf);
	
reschedule:
	event_add_read(master, bgpmgmt_socket_read, bsc, bsc->sock, &bsc->inbufev);
/**
 * 包分片逻辑
 */
	// if (bsc->inbuf->endp == bsc->inbuf->size)
	// 	goto skip_read;

// read_again:
// 	/* Attempt to read message from client. */
// 	rv = stream_read_try(bsc->inbuf, bsc->sock, STREAM_WRITEABLE(bsc->inbuf));
// 	if (rv == 0) {
// 		zlog_info("%s: socket closed", __func__);
// 		bgpmgmt_socket_ctx_free(bsc);
// 		return -1;
// 	}
// 	if (rv == -1) {
// 		zlog_warn("%s: socket failed: %s", __func__, strerror(errno));
// 		bgpmgmt_socket_ctx_free(bsc);
// 		return -1;
// 	}

// 	if (rv == -2)
// 		return -2;
	
// 	/* Account read bytes. */
// 	bsc->in_bytes += (uint64_t)rv;
// 	/* Register peak buffered bytes. */
// 	rlen = STREAM_READABLE(bsc->inbuf);
// 	if (bsc->in_bytes_peak < rlen)
// 		bsc->in_bytes_peak = rlen;

// skip_read:
// 	while (rlen > 0) {
// 		/* Account full message read. */
// 		data = (char *)stream_pnt(bsc->inbuf);
// 		data_len = STREAM_READABLE(bsc->inbuf);
// 		/* Not enough data read. */ /* you: 注意！ */
// 		/**
// 		 * 包分片，可能会导致数据不完整，需要重新读取
// 		 */
// 		if (ntohs(data_len) > rlen)
// 			goto read_again;
		
// 		/* Account full message read. */
// 		bsc->in_msgs++;

// 		/* Account this message as whole read for buffer reorganize. */
// 		reads++;

// 		/**
// 		 * Handle incoming message. with callback if the ID matches,
// 		 * otherwise fallback to default handler.
// 		 */
// 		if (id && ntohs(data_len) == id)
// 			cb(data, arg);
// 		else
// 			/* you: default handler */
// 			zlog_info("%s: get message: %s", __func__, data);
		
// 		/* Advance current read pointer. */
// 		stream_forward_getp(bsc->inbuf, ntohs(data_len));

// 		/* Reduce the buffer available bytes. */
// 		rlen -= ntohs(data_len);

// 		/* Reorganize buffer to handle more bytes read. */
// 		if (reads >= 3) {
// 			stream_pulldown(bsc->inbuf);
// 			reads = 0;
// 		}

// 		/* We found the message, return to caller. */
// 		if (id && ntohs(data_len) == id)
// 			break;
// 	}

	return 0;
}

/**
 * 从socket读取数据
 */
static void bgpmgmt_socket_read(struct event *t)
{
	struct bgpmgmt_socket_ctx *bsc = EVENT_ARG(t);
	int rv;

	/* 从socket读取数据的部分，其中cb为所需要的回调函数 */
	rv = bgpmgmt_socket_expect(bsc, 0, NULL, NULL);

	if (rv == -1)
		return;

	stream_pulldown(bsc->inbuf);

	/* 事件循环添加下一次读取任务 */
	event_add_read(master, bgpmgmt_socket_read, bsc, bsc->sock, &bsc->inbufev);
}

/**
 * 创建新的bgpmgmt_socket_ctx
 */
static struct bgpmgmt_socket_ctx *bgpmgmt_socket_ctx_new(int sock)
{
	struct bgpmgmt_socket_ctx *bsc;

	bsc = XCALLOC(MTYPE_BGPMGMT_SOCKET_CTX,  sizeof(*bsc));

	bsc->sock = sock;
	bsc->inbuf = stream_new(BGPMGMT_SOCKET_BUF_SIZE);
	bsc->outbuf = stream_new(BGPMGMT_SOCKET_BUF_SIZE);

	/* If not socket ready, skip read and session registration. */
	if (sock == -1)
		return bsc;

	/* 准备读取数据 */
	event_add_read(master, bgpmgmt_socket_read, bsc, sock, &bsc->inbufev);

	return bsc;
}


/**
 * listen for incoming connections on the management socket
 * 启动新的连接
 */
static void mgmt_socket_accept(struct event *t)
{
    struct _bgpmgmt_global *bg = EVENT_ARG(t);
	struct bgpmgmt_socket_ctx *bsc;
    int sock;

    /* Accept new connection. */
    sock = accept(bg->bgpmgmt_sock, NULL, 0);
    if (sock == -1) {
        zlog_warn("%s: accept failed: %s", __func__, strerror(errno));
        goto reschedule_and_return;
    }

    /* Create and handle new connection. */
	/* 为当前连接创建socket handler */
    bsc = bgpmgmt_socket_ctx_new(sock);
	TAILQ_INSERT_TAIL(&bgpmgmt_global.bgpmgmt_sockq, bsc, entry);

reschedule_and_return:
	event_add_read(master, mgmt_socket_accept, bg, bg->bgpmgmt_sock,
		       &bgpmgmt_global.bgpmgmt_sockev);
}

static int mgmt_socket_finish_late(void)
{
	struct bgpmgmt_socket_ctx *bsc;

	zlog_debug("%s: terminating bgpmgmt", __func__);

	while ((bsc = TAILQ_FIRST(&bgpmgmt_global.bgpmgmt_sockq)) != NULL)
		bgpmgmt_socket_ctx_free(bsc);

    EVENT_OFF(bgpmgmt_global.bgpmgmt_sockev);
    close(bgpmgmt_global.bgpmgmt_sock);

    return 0;
}

int mgmt_socket_init(const struct sockaddr *sa, socklen_t salen, bool client)
{
	int sock;
    zlog_debug("%s: initializing management", __func__);

	TAILQ_INIT(&bgpmgmt_global.bgpmgmt_sockq);

	bgpmgmt_global.bgpmgmt_sock = -1;

	/* daemon销毁时的注册函数 */
    hook_register(frr_fini, mgmt_socket_finish_late);

    sock = socket(sa->sa_family, SOCK_STREAM, 0);
    if (sock == -1) {
        zlog_warn("%s: failed to initialize socket: %s", __func__, strerror(errno));
        return -1;
    }

    if (sockopt_reuseaddr(sock) == -1) {
        zlog_warn("%s: failed to set reuseaddr: %s", __func__, strerror(errno));
        close(sock);
        return -1;
    }

    if (sa->sa_family == AF_UNIX)
        unlink(((struct sockaddr_un *)sa)->sun_path);

    if (bind(sock, sa, salen) == -1) {
        zlog_warn("%s: failed to bind socket: %s", __func__, strerror(errno));
        close(sock);
        return -1;
    }

    if (listen(sock, SOMAXCONN) == -1) {
		zlog_warn("%s: failed to put socket on listen: %s", __func__,
			  strerror(errno));
		close(sock);
		return -1;
	}

    bgpmgmt_global.bgpmgmt_sock = sock;
    event_add_read(master, mgmt_socket_accept, &bgpmgmt_global, 
        sock, &bgpmgmt_global.bgpmgmt_sockev);

	zlog_notice("%s: side socket server initialized on socket %d", __func__, sock);
    return 0;
}