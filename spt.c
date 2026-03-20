/* See LICENSE file for copyright and license details. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef NOTIFY
#include <libnotify/notify.h>
#endif /* NOTIFY */

#include "config.h"

/* macros */
#define LEN(a)  (sizeof(a) / sizeof(a[0]))
#define MAX_PATH 4096

static volatile sig_atomic_t suspend;
static volatile int restart_timer = 0;

static int timer_idx = 0;
static int remaining_sec = 0;
static char status_path[MAX_PATH];

/* function declarations */
static void clean_status_file(void);
static void die(const char *errstr, ...);
static void init_status_file(void);
static void log_state(void);
static void notify_send(char *cmt);
static void notify_state(void);
static void register_signal(int signal, 
                            void (*callback_fn)(int),
                            const char* error_message,
                            struct sigaction * sigaction);
static void spawn(char *argv[]);
static void sig_reset_timer(int sig);
static void sig_shutdown_handler(int sig);
static void sig_skip_timer(int sig);
static void sig_suspend_timer(int sigint);

typedef struct {
    int sig;
    void (*callback_fn)(int);
    const char* error_message;
} SignalHandler;

static const SignalHandler sig_handlers_exit[] = {
    { SIGINT,  sig_shutdown_handler, "cannot associate SIGINT to handler\n" },
    { SIGTERM, sig_shutdown_handler, "cannot associate SIGTERM to handler\n" },
    { SIGHUP,  sig_shutdown_handler, "cannot associate SIGHUP to handler\n" }
};

static const SignalHandler sig_handlers_ctrl[] = {
    { 1, sig_suspend_timer, "cannot associate suspend signal to handler\n" },
    { 2, sig_skip_timer, "cannot associate skip signal to handler\n" },
    { 3, sig_reset_timer, "cannot associate reset signal to handler\n" },
};

/* functions implementations */

void
clean_status_file(void)
{
    remove(status_path);
}

void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

void
init_status_file(void)
{
	uid_t uid;
	struct stat st;
	const char *xdg_runtime;
    FILE* status_file;

	if ((xdg_runtime = getenv("XDG_RUNTIME_DIR")) != NULL)
		snprintf(status_path, sizeof(status_path), "%s/spt/", xdg_runtime);
	else {
		uid = getuid();
		snprintf(status_path, sizeof(status_path), "/run/user/%u/spt/", uid);
	}

	if (stat(status_path, &st) == -1) {
		if (mkdir(status_path, 0700) == -1)
			die("cannot create path for status file");
	}

    strcat(status_path, "status");

    printf("Create %s\n", status_path);
	status_file = fopen(status_path, "w");
	if (status_file == NULL)
		die("cannot create status file");
    fclose(status_file);
}

void
log_state(void)
{
	char buf[36];
	snprintf(buf, 36, "%s %02d:%02d%s",
         (timer_idx & 1 ? "break" : "focus"),
         remaining_sec / 60,
		 remaining_sec % 60,
		 (suspend) ? " paused" : "" );
    FILE* status_file = fopen(status_path, "w");
    fprintf(status_file, "%s", buf);
    fclose(status_file);
}

void
notify_send(char *cmt)
{
	if (strcmp(notifycmd, ""))
		spawn((char *[]) { notifycmd, "spt", cmt, NULL });
#ifdef NOTIFY
	else {
		notify_init("spt");
		NotifyNotification *n = notify_notification_new("Pomodoro", cmt, \
					"alarm-clock");
		notify_notification_show(n, NULL);
		g_object_unref(G_OBJECT(n));
		notify_uninit();
	}
#endif /* NOTIFY */

	if (strcmp(notifyext, "")) /* extra commands to use */
		spawn((char *[]) { "/bin/sh", "-c", notifyext, NULL });
}

void
notify_state(void)
{
	char buf[64];
	snprintf(buf, 64, "%s time%s %02d:%02d",
		 (timer_idx & 1 ? "Break" : "Focus"),
         (suspend ? " paused on" : ""),
         remaining_sec / 60,
		 remaining_sec % 60);
    notify_send(buf);
}

void register_signal(int signal, 
                     void(*callback_fn)(int),
                     const char* error_message,
                     struct sigaction * sa)
{
	sa->sa_handler = callback_fn;
	sigemptyset(&sa->sa_mask);
	sa->sa_flags = 0;

	if (sigaction(signal, sa, NULL) == -1)
		die(error_message);
}

void
sig_reset_timer(int sig)
{
    timer_idx = -1;
    restart_timer = 1;
}

void
sig_shutdown_handler(int sig)
{
	clean_status_file();
	exit(0);
}

void 
sig_skip_timer(int sig) 
{
    restart_timer = 1;
}

void
sig_suspend_timer(int sigint)
{
	suspend ^= 1;
    notify_state();
}

void
spawn(char *argv[])
{
	if (fork() == 0) {
		setsid();
		execvp(argv[0], argv);
		die("spt: execvp %s\n", argv[0]);
		perror(" failed");
		exit(0);
	}
}

int
main(int argc, char *argv[])
{
    init_status_file();
	struct timespec remaining;
	struct sigaction sa;
	sigset_t emptymask;
	int i;

    /* Exit signals (SIGINT, SIGTERM, SIGHUP) */
    for (i = 0; i < LEN(sig_handlers_exit); i++) {
        register_signal(sig_handlers_exit[i].sig, 
                        sig_handlers_exit[i].callback_fn,
                        sig_handlers_exit[i].error_message, &sa);
    }
    /* Control signals: suspend, skip, reset */
    for (i = 0; i < LEN(sig_handlers_ctrl); i++) {
        register_signal(sig_handlers_ctrl[i].sig + SIGRTMIN, 
                        sig_handlers_ctrl[i].callback_fn,
                        sig_handlers_ctrl[i].error_message, &sa);
    }
	sigemptyset(&emptymask);

    // sigsuspend(&emptymask);
	for (timer_idx = 0; ; timer_idx = (timer_idx + 1) % LEN(timers)) {
        remaining_sec = timers[timer_idx];
	    notify_state();
        while (remaining_sec > 0) {
            remaining.tv_sec = 1;
            remaining.tv_nsec = 0;

            log_state();
            while (remaining.tv_sec) {
                if (suspend)
                    sigsuspend(&emptymask);
                else if (restart_timer || nanosleep(&remaining, &remaining) == 0)
                        remaining.tv_sec = remaining.tv_nsec = 0;
            }

            if (restart_timer) {
                restart_timer = 0;
                break;
            }

            remaining_sec--;
        }
    }

	return 0;
}
