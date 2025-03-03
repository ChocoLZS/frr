#include <zebra.h>

#include <lib/version.h>

#include "helloworld/helloworld.h"

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

static struct frr_signal_t helloworld_signals[] = {
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

struct zebra_privs_t helloworld_privs = {
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

static struct frr_daemon_info helloworld_di;

FRR_DAEMON_INFO(helloworld, BGPMGMTD,
    .vty_port = HELLOWORLD_VTY_PORT,
    .proghelp = "Implementation of your first helloworld Daemon.",

    .signals = helloworld_signals,
    .n_signals = array_size(helloworld_signals),

    .privs = &helloworld_privs,
);

int main(int argc, char **argv)
{
    int opt;

    frr_preinit(&helloworld_di, argc, argv);

    while (true) {
		opt = frr_getopt(argc, argv, NULL);
		if (opt == EOF)
			break;
    }

    /* Initialize FRR infrastructure. */
	master = frr_init();

	frr_config_fork();

    /* not the recommended way to log, you should use zlog instead of fprintf */
    fprintf(stdout, "helloworld! your first deamon helloworld has started.\n");

	frr_run(master);

	/* Not reached. */
	return 0;
}