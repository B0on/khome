/*
 * fakekaf - minimal stand-in for the Kindle LIPC service com.lab126.kaf
 *
 * Purpose: powerd refuses to act on the power button while it believes the
 * boot splash is still up.  In firmware 5.12.2.1.1 powerd decides that with
 * (see docs/spikes/fakekaf.md, powerd function at 0x16634):
 *
 *   - LipcGetIntProperty(com.lab126.kaf, "frameworkStarted") must succeed
 *     and the value must be non-zero, and
 *   - LipcGetStringProperty(com.lab126.blanket, "load") must not contain
 *     the token "splash".
 *
 * With cvm stopped there is no com.lab126.kaf, so the first lookup fails and
 * powerd logs "Splash screen is on.  Ignoring power button".  This program
 * registers the service and answers frameworkStarted=1.  It can also unload
 * the blanket "splash" module (what framework_setup would normally do) so
 * that the check passes when the GUI is never started (DONT_START_FRAMEWORK).
 *
 * This is not a replacement for the Java framework: it does not implement
 * any of the other com.lab126.kaf properties or events.  It exists only to
 * satisfy powerd's gate.
 *
 * It also registers com.lab126.booklet.home as a write-only stand-in for the
 * home booklet.  appmgrd stops an app without raising its "application error"
 * alert only when it can hand GO to a home booklet; with cvm gone there is
 * none, so fakekaf answers appmgrd's five requests with success.
 *
 * The LIPC API used here is the openlipc reverse-engineered header
 * (openlipc.h, MIT license, see LICENSE.openlipc).  It is ABI-compatible
 * with the device's /usr/lib/liblipc.so.1.
 */

#define _POSIX_C_SOURCE 200809L

#include "openlipc.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define KAF_SERVICE "com.lab126.kaf"
#define KAF_PROPERTY "frameworkStarted"
#define KAF_EVENT "FrameworkStarted"

#define BLANKET_SERVICE "com.lab126.blanket"
#define BLANKET_LOAD_PROPERTY "load"
#define BLANKET_UNLOAD_PROPERTY "unload"
#define BLANKET_SPLASH_MODULE "splash"

#define HOME_SERVICE "com.lab126.booklet.home"
#define APPMGRD_SERVICE "com.lab126.appmgrd"

/* appmgrd delivers these to the home booklet and reads <name>result back. */
static const char *const g_home_properties[] = {
    "load", "go", "pause", "unload", "run"
};

static volatile sig_atomic_t g_stop = 0;
static int g_started = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

/*
 * Deliberately avoid stdio's stdout/stderr objects.  lld links references to
 * them as GLOB_DAT relocations, which the device's glibc 2.20 ld.so trips
 * over (the binary segfaults in _dl_relocate_object before main).  Plain
 * write(2) keeps the dynamic symbol table free of those data symbols.
 */
static void write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, buf, len);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        buf += n;
        len -= (size_t)n;
    }
}

static void logmsg(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if ((size_t)n >= sizeof(buf) - 1) {
        n = (int)sizeof(buf) - 2;
    }
    buf[n++] = '\n';
    write_all(STDERR_FILENO, buf, (size_t)n);
}

static LIPCcode get_started(LIPC *lipc, const char *property, void *value,
                            void *data)
{
    (void)lipc;
    (void)property;
    (void)data;

    LIPC_GETTER_VTOI(value) = g_started;
    return LIPC_OK;
}

/*
 * com.lab126.booklet.home stand-in.  appmgrd's stop path gives the request to
 * the home booklet as a string value "<serial>:..." and waits for
 * "<serial>:0" on com.lab126.appmgrd.<name>result.  Every request succeeds
 * here, because this process only exists to let the browser be stopped
 * without appmgrd raising its "application error" alert.
 */
static LIPCcode home_set(LIPC *lipc, const char *property, void *value,
                         void *data)
{
    const char *text = LIPC_SETTER_VTOS(value);
    const char *colon;
    char result[64];
    char reply[128];
    size_t serial_len;
    LIPCcode code;

    (void)data;
    if (text == NULL) {
        return LIPC_OK;
    }
    colon = strchr(text, ':');
    serial_len = colon != NULL ? (size_t)(colon - text) : strlen(text);
    if (serial_len > sizeof(reply) - 4) {
        serial_len = sizeof(reply) - 4;
    }
    snprintf(result, sizeof(result), "%sresult", property);
    snprintf(reply, sizeof(reply), "%.*s:0", (int)serial_len, text);

    code = LipcSetStringProperty(lipc, APPMGRD_SERVICE, result, reply);
    if (code != LIPC_OK) {
        logmsg("fakekaf: cannot set %s.%s (%s)", APPMGRD_SERVICE, result,
               LipcGetErrorString(code));
    }
    return LIPC_OK;
}

/* Return 1 if the whitespace-separated module list contains "splash". */
static int list_has_splash(const char *list)
{
    const char *p = list;
    size_t want = strlen(BLANKET_SPLASH_MODULE);

    while (*p != '\0') {
        size_t n;

        while (*p == ' ' || *p == '\t') {
            p++;
        }
        n = 0;
        while (p[n] != '\0' && p[n] != ' ' && p[n] != '\t') {
            n++;
        }
        if (n == want && memcmp(p, BLANKET_SPLASH_MODULE, want) == 0) {
            return 1;
        }
        p += n;
    }
    return 0;
}

/*
 * Ask blanket to unload the splash module if it is loaded.  Returns 0 on
 * success (including "already unloaded" and "blanket not available"), -1 on
 * a real LIPC error while unloading.
 */
static int unload_splash(LIPC *lipc, int quiet)
{
    char *load = NULL;
    LIPCcode code;

    code = LipcGetStringProperty(lipc, BLANKET_SERVICE, BLANKET_LOAD_PROPERTY,
                                 &load);
    if (code != LIPC_OK || load == NULL) {
        if (!quiet) {
            logmsg("fakekaf: cannot read %s.%s (%s); leaving splash alone",
                   BLANKET_SERVICE, BLANKET_LOAD_PROPERTY,
                   LipcGetErrorString(code));
        }
        if (load != NULL) {
            LipcFreeString(load);
        }
        return 0;
    }

    if (!list_has_splash(load)) {
        if (!quiet) {
            logmsg("fakekaf: blanket splash module already unloaded");
        }
        LipcFreeString(load);
        return 0;
    }
    LipcFreeString(load);

    if (!quiet) {
        logmsg("fakekaf: unloading blanket splash module");
    }
    code = LipcSetStringProperty(lipc, BLANKET_SERVICE,
                                 BLANKET_UNLOAD_PROPERTY,
                                 BLANKET_SPLASH_MODULE);
    if (code != LIPC_OK) {
        logmsg("fakekaf: failed to unload blanket splash (%s)",
               LipcGetErrorString(code));
        return -1;
    }

    return 0;
}

static void usage(const char *argv0)
{
    char buf[1200];
    int n;

    n = snprintf(buf, sizeof(buf),
        "usage: %s [options]\n"
        "\n"
        "Registers the LIPC service %s and serves:\n"
        "  int %s = 1\n"
        "\n"
        "Also registers %s as the home booklet stand-in that appmgrd's stop\n"
        "path needs (load, go, pause, unload, run).\n"
        "\n"
        "Options:\n"
        "  --value N        value returned for %s (default 1)\n"
        "  --no-event      do not send the %s event on startup\n"
        "  --keep-splash   do not unload blanket's splash module\n"
        "  --quiet         suppress informational messages\n"
        "  --selftest      read a couple of LIPC properties and exit; does\n"
        "                  not register a service (safe on a running device)\n"
        "  --help          show this help and exit (does not touch LIPC)\n"
        "  --version       show version and exit (does not touch LIPC)\n",
        argv0, KAF_SERVICE, KAF_PROPERTY, HOME_SERVICE, KAF_PROPERTY,
        KAF_EVENT);
    if (n < 0) {
        return;
    }
    if ((size_t)n >= sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
    }
    write_all(STDOUT_FILENO, buf, (size_t)n);
}

/*
 * Read-only check that this binary can load liblipc and talk to the daemon.
 * It opens an unnamed handle, so it never claims com.lab126.kaf.
 */
static int selftest(void)
{
    LIPC *lipc;
    LIPCcode code;
    char *load = NULL;
    char *state = NULL;
    int rc = 0;

    lipc = LipcOpenNoName();
    if (lipc == NULL) {
        logmsg("fakekaf: selftest: LipcOpenNoName failed");
        return 1;
    }

    code = LipcGetStringProperty(lipc, BLANKET_SERVICE, BLANKET_LOAD_PROPERTY,
                                 &load);
    if (code == LIPC_OK && load != NULL) {
        logmsg("fakekaf: selftest: %s.%s = [%s]", BLANKET_SERVICE,
               BLANKET_LOAD_PROPERTY, load);
        LipcFreeString(load);
    } else {
        logmsg("fakekaf: selftest: cannot read %s.%s (%s)", BLANKET_SERVICE,
               BLANKET_LOAD_PROPERTY, LipcGetErrorString(code));
        rc = 1;
    }

    code = LipcGetStringProperty(lipc, "com.lab126.powerd", "state", &state);
    if (code == LIPC_OK && state != NULL) {
        logmsg("fakekaf: selftest: com.lab126.powerd.state = [%s]", state);
        LipcFreeString(state);
    } else {
        logmsg("fakekaf: selftest: cannot read powerd state (%s)",
               LipcGetErrorString(code));
        rc = 1;
    }

    LipcClose(lipc);
    return rc;
}

int main(int argc, char **argv)
{
    LIPC *lipc;
    LIPC *home = NULL;
    LIPCcode code = LIPC_OK;
    LIPCcode home_code = LIPC_OK;
    struct sigaction sa;
    int send_event = 1;
    int do_unload_splash = 1;
    int quiet = 0;
    int i;
    size_t p;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            write_all(STDOUT_FILENO, "fakekaf 0.1\n", 12);
            return 0;
        }
        if (strcmp(argv[i], "--selftest") == 0) {
            return selftest();
        }
        if (strcmp(argv[i], "--no-event") == 0) {
            send_event = 0;
            continue;
        }
        if (strcmp(argv[i], "--keep-splash") == 0) {
            do_unload_splash = 0;
            continue;
        }
        if (strcmp(argv[i], "--quiet") == 0) {
            quiet = 1;
            continue;
        }
        if (strcmp(argv[i], "--value") == 0) {
            char *end = NULL;
            long v;

            if (i + 1 >= argc) {
                logmsg("fakekaf: --value needs an argument");
                return 2;
            }
            errno = 0;
            v = strtol(argv[++i], &end, 0);
            if (errno != 0 || end == argv[i] || *end != '\0') {
                logmsg("fakekaf: invalid --value '%s'", argv[i]);
                return 2;
            }
            g_started = (int)v;
            continue;
        }
        logmsg("fakekaf: unknown option '%s' (try --help)", argv[i]);
        return 2;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    lipc = LipcOpenEx(KAF_SERVICE, &code);
    if (lipc == NULL) {
        logmsg("fakekaf: cannot register %s (%s)", KAF_SERVICE,
               LipcGetErrorString(code));
        return 1;
    }

    code = LipcRegisterIntProperty(lipc, KAF_PROPERTY, get_started, NULL, NULL);
    if (code != LIPC_OK) {
        logmsg("fakekaf: cannot register %s.%s (%s)", KAF_SERVICE,
               KAF_PROPERTY, LipcGetErrorString(code));
        LipcClose(lipc);
        return 1;
    }

    /* Second name on the same process.  fakekaf has no loop of its own: the
     * library dispatches property callbacks while main sleeps, as it already
     * does for the kaf getter.  Failing to register this one is not fatal,
     * because powerd only needs com.lab126.kaf. */
    home = LipcOpenEx(HOME_SERVICE, &home_code);
    if (home == NULL) {
        logmsg("fakekaf: cannot register %s (%s); continuing as kaf only",
               HOME_SERVICE, LipcGetErrorString(home_code));
    } else {
        for (p = 0; p < sizeof(g_home_properties) / sizeof(g_home_properties[0]);
             p++) {
            home_code = LipcRegisterStringProperty(home, g_home_properties[p],
                                                   NULL, home_set, NULL);
            if (home_code != LIPC_OK) {
                logmsg("fakekaf: cannot register %s.%s (%s)", HOME_SERVICE,
                       g_home_properties[p], LipcGetErrorString(home_code));
            }
        }
    }

    if (do_unload_splash) {
        /* A failure here is not fatal: powerd re-checks blanket itself. */
        (void)unload_splash(lipc, quiet);
    }

    if (send_event) {
        code = LipcCreateAndSendEvent(lipc, KAF_EVENT);
        if (code != LIPC_OK) {
            logmsg("fakekaf: warning: cannot send %s event (%s)", KAF_EVENT,
                   LipcGetErrorString(code));
        }
    }

    if (!quiet) {
        logmsg("fakekaf: registered %s, %s=%d%s%s", KAF_SERVICE,
               KAF_PROPERTY, g_started, send_event ? ", sent " : "",
               send_event ? KAF_EVENT : "");
    }

    while (!g_stop) {
        sleep(1);
    }

    if (!quiet) {
        logmsg("fakekaf: exiting");
    }
    if (home != NULL) {
        LipcClose(home);
    }
    LipcClose(lipc);
    return 0;
}
