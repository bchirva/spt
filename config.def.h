/* See LICENSE file for copyright and license details. */

/* Notification, remove DNOTIFY in config.mk if you don't want it */
static char *notifycmd = ""; /* Uses given command if not compiled by DNOTIFY */
static char *notifyext = ""; /* Notify with extra command (eg. play an alarm) */

/*
 * This is the array which defines all the timer that will be used.
 * It will be repeated after all of it is executed.
 */
static const int timers[] = {
    1500, 300,
    1500, 300,
    1500, 300,
    1500, 900
};
