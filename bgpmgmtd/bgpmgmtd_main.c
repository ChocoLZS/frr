#include <stdio.h>
#include <zebra.h>

#include <lib/version.h>
#include "libfrr.h"
#include "log.h"

#include "bgpmgmtd/bgpmgmtd.h"

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

int main(int argc, char **argv)
{
    frr_preinit(&bgpmgmtd_di, argc, argv);

    /* Initialize FRR infrastructure. */
	master = frr_init();
    // write something to file
    FILE *file = fopen("/tmp/bgpmgmtd.txt", "w");
    if (file == NULL) {
        zlog_err("Failed to open file");
        return 1;
    }

    fprintf(file, "Hello, this is the message from bgpmgmtd!\n");
    fclose(file);

    zlog_info("bgpmgmtd is running!");

    frr_config_fork();

    frr_run(master);

    return 0;
}