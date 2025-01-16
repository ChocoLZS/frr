#include <stdio.h>
#include <zebra.h>

#include <lib/version.h>

#include "bgpmgmtd/bgpmgmt.h"
// #include "bgpmgmtd/side_socket.h"

/* Master of threads. */
struct event_loop *master;

/* signal definitions */
void sighup(void);
void sigint(void);
void sigusr1(void);

/* SIGHUP handler. */
void sighup(void)
{
	zlog_info("SIGHUP received, ignoring");

	return;
}

/* SIGUSR1 handler. */
void sigusr1(void)
{
	zlog_rotate();
}

/* SIGINT handler. */
__attribute__((__noreturn__)) void sigint(void)
{
	zlog_notice("Terminating on signal");
    /* Signalize shutdown. */
	frr_early_fini();

    /* Terminate and free() FRR related memory. */
	frr_fini();

	exit(0);
}

static struct frr_signal_t bgpmgmt_signals[] = {
	{
		.signal = SIGHUP,
		.handler = &sighup,
	},
	{
		.signal = SIGUSR1,
		.handler = &sigusr1,
	},
	{
		.signal = SIGINT,
		.handler = &sigint,
	},
	{
		.signal = SIGTERM,
		.handler = &sigint,
	},
};

/* privileges */
static zebra_capabilities_t _caps_p[] = {ZCAP_BIND, ZCAP_NET_RAW,
					 ZCAP_NET_ADMIN, ZCAP_SYS_ADMIN};

struct zebra_privs_t bgpmgmtd_privs = {
#if defined(FRR_USER) && defined(FRR_GROUP)
	.user = FRR_USER,
	.group = FRR_GROUP,
#endif
#ifdef VTY_GROUP
	.vty_group = VTY_GROUP,
#endif
	.caps_p = _caps_p,
	.cap_num_p = array_size(_caps_p),
	.cap_num_i = 0,
};

static struct frr_daemon_info bgpmgmtd_di;

FRR_DAEMON_INFO(bgpmgmtd, BGPMGMTD,
    .vty_port = BGPMGMTD_VTY_PORT,
    .proghelp = "Implementation of the BGP Management Daemon.",

    .signals = bgpmgmt_signals,
    .n_signals = array_size(bgpmgmt_signals),

    .privs = &bgpmgmtd_privs,
);

struct _bgpmgmt_global bgpmgmt_global;

int main(int argc, char **argv)
{
    int opt;
    struct sockaddr_in addr;

    frr_preinit(&bgpmgmtd_di, argc, argv);
    /* 处理命令行参数，如下处理逻辑不能删除 */
    while (true) {
		opt = frr_getopt(argc, argv, NULL);
		if (opt == EOF)
			break;
    }

    /* Initialize FRR infrastructure. */
	master = frr_init();



    frr_config_fork();

	/* Initialize bgpmgmt infrastructure for api exposing. */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(25565);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	zlog_info("Initializing bgpmgmt socket");

	if(mgmt_socket_init((struct sockaddr *)&addr, sizeof(addr), false) < 0) {
		zlog_err("Failed to initialize bgpmgmt socket");
		exit(1);
	}
	zlog_notice("bgpmgmt socket initialized");

    frr_run(master);

    /* Not reached. */
    return 0;
}