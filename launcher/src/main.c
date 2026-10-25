#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "canvas.h"
#include "fb.h"
#include "gesture.h"
#include "input.h"
#include "kindletimes.h"
#include "layout.h"
#include "lipc.h"
#include "state.h"
#include "tabs.h"
#include "text.h"
#include "urledit.h"

#define KHOME_DIR "/mnt/us/khome"
#define KHOME_LOG_FILE KHOME_DIR "/khome.log"
#define KHOME_DISABLE_FILE KHOME_DIR "/disable"
#define KHOME_TABS_FILE KHOME_DIR "/tabs.txt"
#define KHOME_FAVORITES_FILE KHOME_DIR "/favorites.tsv"
#define KHOME_NEWS_TEXT_SIZE_FILE KHOME_DIR "/news-text-size"
#define KHOME_CHARGE_LIMIT_FILE KHOME_DIR "/charge-limit"
#define KHOME_CV_PRM                                                        \
    "/sys/devices/system/wario_charger/wario_charger0/cv_prm"
#define KHOME_BATTERY_MIX                                                   \
    "/sys/devices/system/wario_battery/wario_battery0/battery_capacity_mix"
#define KHOME_FONT_DIR KHOME_DIR "/fonts"
#define KHOME_ICON_DIR KHOME_DIR "/icons"
#define KHOME_KOREADER "/mnt/us/koreader/koreader.sh"
#define KHOME_KOREADER_KILLED "/var/tmp/khome.koreader-killed"
#define KHOME_NEWS_TEXT_DIR "/mnt/us/newspaper/text/current"
#define KHOME_NEWS_FETCH "/mnt/us/extensions/newspaper/bin/fetch.sh"
#define KHOME_XEXPOSE KHOME_DIR "/bin/xexpose"
#define KHOME_XBAND KHOME_DIR "/bin/xband"
#define KHOME_TOOL_PATH "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
#define KHOME_DEFAULT_URL "https://www.google.co.jp/"
#define KHOME_ENCODED_MAX (KHOME_TAB_URL_MAX * 3 + 1)
/* The browser's LocalStorage DB: ItemTable holds lastUrl (the page on screen)
 * and history (an array whose newest entry for that URL carries its title). */
#define KHOME_SQLITE3 "/usr/bin/sqlite3"
#define KHOME_BROWSER_DB                                                     \
    "/mnt/us/.active_content_sandbox/browser/resource/LocalStorage/"         \
    "file__0.localstorage"
#define KHOME_BROWSER_ROW_MAX                                                \
    (4 * (KHOME_TAB_URL_MAX + KHOME_PAGE_TITLE_MAX) + 8)
/* The settings object is 72 characters today; this leaves room for a few
 * more browser options without ever truncating a write-back. */
#define KHOME_BROWSER_SETTINGS_MAX 2048
#define KHOME_BROWSER_SETTINGS_HEX_MAX (KHOME_BROWSER_SETTINGS_MAX * 4 + 1)
#define KHOME_BROWSER_SETTINGS_ROW_MAX (KHOME_BROWSER_SETTINGS_HEX_MAX + 32)
/* winmgr repaints and rotates back as the browser's window goes, but not
 * necessarily before its process is reaped. */
#define KHOME_PORTRAIT_WAIT_MS 1000
#define KHOME_FAVORITES_TEXT_MAX                                             \
    (KHOME_MAX_FAVORITES * (KHOME_TAB_URL_MAX + KHOME_FAVORITE_NAME_MAX + 2) \
     + 8)
#define KHOME_APP_BROWSER "com.lab126.browser"
#define KHOME_BROWSER_COMM "mesquite"
#define KHOME_BROWSER_POLL_MS 100
/* kill 1 removed mesquite in about 2 s in the no-cvm spike; give it 5 s. */
#define KHOME_KILL_TIMEOUT_MS 5000
/* appmgrd raises its error alert right after the browser is reaped. */
#define KHOME_ALERT_TIMEOUT_MS 5000
#define KHOME_ALERT_DISMISS_MS 10000
/* The alert's close button, measured on the Paperwhite 2. */
#define KHOME_ALERT_X 597
#define KHOME_ALERT_Y 602
/* The browser is spawned by appmgrd in 1.5-2.2 s; 15 s means it never came. */
#define KHOME_BROWSER_START_TIMEOUT_MS 15000
/* appmgrd spawns the browser in 1.5-2.2 s.  A tap or top-edge pull after this
 * long is the user giving up on the launch screen, not a stray touch. */
#define KHOME_BROWSER_CANCEL_GRACE_MS 3000
#define KHOME_SCALE (758.0f / 1072.0f)
#define KHOME_WAKE_DELAY_MS 500
#define KHOME_LONG_POLL_MS 1000
#define KHOME_WATCH_RETRY_MS 5000
/* A hung wpa_cli or lipc-get-prop must not freeze the poll loop. */
#define KHOME_CMD_TIMEOUT_MS 3000
#define KHOME_WIFI_POLL_MS 3000
#define KHOME_WIFI_POLL_COUNT 5
/* Measured on the PW2: flIntensity accepts and reads back 0..25. */
#define KHOME_FL_MAX 25
/* The new shield has to be mapped before the old one goes, or the browser
 * repaints the sheet through the gap.  300 ms was enough on the device. */
#define KHOME_SHIELD_OVERLAP_MS 300

static const char *const month_names[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const char *const weekday_names[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static FILE *log_file;
static Fb fb;
static Canvas canvas;
static Fonts fonts;
static Icons icons;
static InputDevice touch;
static Gesture gesture;
static LipcWatch power_watch;
static LipcWatch app_watch;
static int64_t power_watch_try;
static int64_t app_watch_try;
static HomeModel model;
static SettingsModel settings;
static KhomeConfirm confirm = KHOME_CONFIRM_NONE;
static TabList tabs;
static TabSheetModel sheet;
static int sheet_index;
static int favorite_index;
static char favorites_text[KHOME_FAVORITES_TEXT_MAX];
static UrlEdit edit;
static char edit_open_url[KHOME_TAB_URL_MAX];
static KhomeEditMode edit_mode = KHOME_EDIT_CURRENT;
static KhomeState state = KHOME_STATE_HOME;
static int asleep;
static int touch_grabbed;
static pid_t koreader_pid;
static pid_t band_pid;
static int64_t band_try;
static pid_t shield_pid;
static int64_t shield_try;
static int shield_height;
static pid_t shield_old_pid;
static int shield_old_height;
static int64_t shield_old_due;
static int shield_last_height;
static int alert_pending;
static int quit;
static int quit_code;
static int wifi_poll_left;
static int64_t wifi_poll_due;
static volatile sig_atomic_t close_requested;
static int sig_pipe[2] = { -1, -1 };
static int64_t defer_due;
static KhomeEvent defer_event;
static int defer_active;
static int64_t browser_since;
static int browser_seen;
static struct timespec last_open_at;
static int last_open_set;
static char page_title[KHOME_PAGE_TITLE_MAX];
static char page_title_url[KHOME_TAB_URL_MAX];
static time_t last_minute;
typedef enum {
    NEWS_VIEW_PAGE = 0,
    NEWS_VIEW_ARTICLE
} NewsView;

typedef enum {
    NEWS_FETCH_FROM_NEWS = 0,
    NEWS_FETCH_FROM_SETTINGS,
    NEWS_FETCH_FROM_MENU
} NewsFetchSource;

static KtPaper news_paper;
static KtArticle news_article;
static int news_have_paper;
static int news_drawn_paper;
static int news_page;
static NewsView news_view;
static int news_origin_page;
static int news_text_size = KT_TEXT_SIZE_DEFAULT;
static char news_edition[128];
static char news_menu_notice[96];
static pid_t fetch_pid;
static char fetch_signature[64];

static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void log_msg(const char *fmt, ...)
{
    va_list ap;
    time_t now = time(NULL);
    struct tm tm;

    if (log_file == NULL) {
        return;
    }
    localtime_r(&now, &tm);
    fprintf(log_file, "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);
    va_start(ap, fmt);
    vfprintf(log_file, fmt, ap);
    va_end(ap);
    fputc('\n', log_file);
    fflush(log_file);
}

/* khome.sh holds the pid-file flock on fd 9.  Children must lose it on exec:
 * an orphaned watcher, KOReader process or plugin server would otherwise keep
 * the next wrapper from taking the lock after khome dies. */
static void lock_fd_close_on_exec(void)
{
    int flags = fcntl(9, F_GETFD);

    if (flags >= 0) {
        fcntl(9, F_SETFD, flags | FD_CLOEXEC);
    }
}

static int log_init(void)
{
    int fd = open(KHOME_LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);

    if (fd < 0) {
        fprintf(stderr, "khome: cannot write %s: %s\n", KHOME_LOG_FILE,
                strerror(errno));
        return -1;
    }
    /* khome.sh truncates the log and appends its own lines to it. */
    log_file = fdopen(fd, "a");
    if (log_file == NULL) {
        close(fd);
        return -1;
    }
    setvbuf(log_file, NULL, _IOLBF, 0);
    return 0;
}

/* Waits for a child until the deadline, then kills and reaps it. */
static int reap_until(pid_t pid, int *status, int64_t deadline)
{
    for (;;) {
        pid_t got = waitpid(pid, status, WNOHANG);

        if (got == pid) {
            return 0;
        }
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (now_ms() >= deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, status, 0) < 0) {
                if (errno != EINTR) {
                    return -1;
                }
            }
            return -1;
        }
        sleep_ms(10);
    }
}

/* Runs argv and returns its stdout and exit status.  A child still running
 * after KHOME_CMD_TIMEOUT_MS is killed, so a hung wpa_cli or lipc call
 * cannot stop the poll loop. */
static int run_capture(char *const argv[], char *out, size_t cap)
{
    int pipefd[2];
    pid_t pid;
    size_t used = 0;
    int status;
    int64_t deadline;
    int timed_out = 0;

    if (out == NULL || cap == 0) {
        return -1;
    }
    out[0] = 0;
    if (pipe(pipefd) < 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        dup2(pipefd[1], STDOUT_FILENO);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        execv(argv[0], argv);
        _exit(127);
    }
    close(pipefd[1]);
    deadline = now_ms() + KHOME_CMD_TIMEOUT_MS;
    while (used + 1 < cap) {
        struct pollfd pfd;
        int left = (int)(deadline - now_ms());
        int ready;
        ssize_t got;

        if (left <= 0) {
            timed_out = 1;
            break;
        }
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;
        pfd.revents = 0;
        ready = poll(&pfd, 1, left);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            timed_out = 1;
            break;
        }
        got = read(pipefd[0], out + used, cap - 1 - used);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (got == 0) {
            break;
        }
        used += (size_t)got;
    }
    out[used] = 0;
    if (timed_out) {
        kill(pid, SIGKILL);
    }
    close(pipefd[0]);
    if (reap_until(pid, &status, deadline) < 0) {
        return -1;
    }
    if (timed_out) {
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }
    return 0;
}

/* Counts processes whose comm is name.  A nonzero sig is sent to each match;
 * sig 0 stops at the first one. */
static int scan_processes(const char *name, int sig)
{
    DIR *dir;
    struct dirent *ent;
    int found = 0;

    dir = opendir("/proc");
    if (dir == NULL) {
        return 0;
    }
    while ((ent = readdir(dir)) != NULL) {
        char path[64];
        char comm[64];
        FILE *fp;
        size_t len;

        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') {
            continue;
        }
        snprintf(path, sizeof(path), "/proc/%s/comm", ent->d_name);
        fp = fopen(path, "r");
        if (fp == NULL) {
            continue;
        }
        if (fgets(comm, sizeof(comm), fp) == NULL) {
            fclose(fp);
            continue;
        }
        fclose(fp);
        len = strlen(comm);
        while (len > 0 && (comm[len - 1] == '\n' || comm[len - 1] == '\r')) {
            comm[--len] = 0;
        }
        if (strcmp(comm, name) == 0) {
            found++;
            if (sig != 0) {
                pid_t pid = (pid_t)strtol(ent->d_name, NULL, 10);
                if (pid > 0) {
                    kill(pid, sig);
                }
            } else {
                break;
            }
        }
    }
    closedir(dir);
    return found;
}

/* A word boundary for the app id: "com.lab126.browserX" must not match, but
 * "com.lab126.browser" at the end of an argument or before a URI separator
 * must. */
static int app_id_word_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int arg_has_app_id(const char *arg)
{
    const char *p = arg;
    size_t n = strlen(KHOME_APP_BROWSER);

    while ((p = strstr(p, KHOME_APP_BROWSER)) != NULL) {
        int left = p == arg || !app_id_word_char(p[-1]);
        int right = !app_id_word_char(p[n]);

        if (left && right) {
            return 1;
        }
        p += n;
    }
    return 0;
}

/* Every WAF app runs as comm "mesquite", so a process with that name is not
 * necessarily the browser.  The store app ("-d com.lab126.stored") is left
 * running by the framework and used to count as the browser, which hid every
 * browser exit and every failed start.  Only the mesquite executable counts,
 * and only when one of its arguments names the browser app id. */
static int browser_running(void)
{
    DIR *dir;
    struct dirent *ent;
    int found = 0;

    dir = opendir("/proc");
    if (dir == NULL) {
        return 0;
    }
    while ((ent = readdir(dir)) != NULL) {
        char path[64];
        char cmd[256];
        FILE *fp;
        size_t got;
        char *arg;
        const char *base;

        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') {
            continue;
        }
        snprintf(path, sizeof(path), "/proc/%s/cmdline", ent->d_name);
        fp = fopen(path, "r");
        if (fp == NULL) {
            continue;
        }
        got = fread(cmd, 1, sizeof(cmd) - 1, fp);
        fclose(fp);
        cmd[got] = 0;
        if (got == 0) {
            continue;
        }
        arg = cmd;
        base = strrchr(arg, '/');
        base = base != NULL ? base + 1 : arg;
        if (strcmp(base, "mesquite") != 0) {
            continue;
        }
        arg += strlen(arg) + 1;
        while (arg < cmd + got) {
            if (arg_has_app_id(arg)) {
                found = 1;
                break;
            }
            arg += strlen(arg) + 1;
        }
        if (found) {
            break;
        }
    }
    closedir(dir);
    return found;
}

/* koreader.sh stops volumd on the way in and only resumes it on its clean exit
 * path, so a crash would leave it stopped and the chroot unmounted. */
static void resume_volumd(void)
{
    if (scan_processes("volumd", SIGCONT) > 0) {
        log_msg("koreader: sent SIGCONT to volumd");
    }
}

static int window_viewable(const char *needle)
{
    static char out[65536];
    char *argv[] = { "/usr/bin/xwininfo", "-root", "-tree", NULL };
    char *line;
    char *save = NULL;
    int found = 0;

    if (run_capture(argv, out, sizeof(out)) < 0) {
        return 0;
    }
    for (line = strtok_r(out, "\n", &save); line != NULL;
         line = strtok_r(NULL, "\n", &save)) {
        if (strstr(line, needle) != NULL &&
            strstr(line, "IsViewable") != NULL) {
            found = 1;
            break;
        }
    }
    return found;
}

static int is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

/* status is the output of `wpa_cli -i wlan0 status`.  Returns 1 only when the
 * interface is connected and copies the ssid line. */
static int parse_wpa_status(const char *status, char *ssid, size_t cap)
{
    int completed = 0;
    const char *p = status;

    if (ssid != NULL && cap > 0) {
        ssid[0] = 0;
    }
    while (p != NULL && *p != 0) {
        const char *eol = strchr(p, '\n');
        size_t len = eol != NULL ? (size_t)(eol - p) : strlen(p);

        while (len > 0 && p[len - 1] == '\r') {
            len--;
        }
        if (len >= 10 && memcmp(p, "wpa_state=", 10) == 0 &&
            len - 10 == 9 && memcmp(p + 10, "COMPLETED", 9) == 0) {
            completed = 1;
        } else if (len >= 5 && memcmp(p, "ssid=", 5) == 0 && ssid != NULL &&
                   cap > 0) {
            size_t copy = len - 5;
            if (copy >= cap) {
                copy = cap - 1;
            }
            memcpy(ssid, p + 5, copy);
            ssid[copy] = 0;
        }
        if (eol == NULL) {
            break;
        }
        p = eol + 1;
    }
    return completed;
}

/* lipc's battLevel jumps close to 100% once charging finishes, so with cv_prm
 * lowered it drifts far from the real charge.  Prefer the panel's own gauge
 * and fall back to lipc only when the node is missing. */
static int battery_mix_read(int *level)
{
    char text[32];
    ssize_t n;
    int fd = open(KHOME_BATTERY_MIX, O_RDONLY);
    int value;

    if (fd < 0) {
        return -1;
    }
    n = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (n <= 0) {
        return -1;
    }
    text[n] = 0;
    if (!isdigit((unsigned char)text[0])) {
        return -1;
    }
    value = atoi(text);
    if (value < 0 || value > 100) {
        return -1;
    }
    *level = value;
    return 0;
}

/* cv_prm reads back "4200mV" and takes a bare millivolt number on write. */
static int cv_prm_read(void)
{
    char text[32];
    ssize_t n;
    int fd = open(KHOME_CV_PRM, O_RDONLY);
    int value;

    if (fd < 0) {
        return -1;
    }
    n = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (n <= 0) {
        return -1;
    }
    text[n] = 0;
    if (!isdigit((unsigned char)text[0])) {
        return -1;
    }
    value = atoi(text);
    return value > 0 ? value : -1;
}

static int cv_prm_write(const char *value)
{
    size_t len = strlen(value);
    int fd = open(KHOME_CV_PRM, O_WRONLY);

    if (fd < 0) {
        log_msg("charge: cannot open %s: %s", KHOME_CV_PRM, strerror(errno));
        return -1;
    }
    if (write(fd, value, len) != (ssize_t)len) {
        log_msg("charge: cannot write %s: %s", KHOME_CV_PRM, strerror(errno));
        close(fd);
        return -1;
    }
    if (close(fd) != 0) {
        log_msg("charge: cannot close %s: %s", KHOME_CV_PRM, strerror(errno));
        return -1;
    }
    return 0;
}

static void model_update(HomeModel *m)
{
    time_t now = time(NULL);
    struct tm tm;
    int battery = -1;

    localtime_r(&now, &tm);
    m->hour = tm.tm_hour;
    m->minute = tm.tm_min;
    m->day_of_year = tm.tm_yday + 1;
    m->days_in_year = is_leap(tm.tm_year + 1900) ? 366 : 365;
    snprintf(m->date, sizeof(m->date), "%s %d", month_names[tm.tm_mon],
             tm.tm_mday);
    snprintf(m->weekday, sizeof(m->weekday), "%s", weekday_names[tm.tm_wday]);
    {
        int pages = m->favorite_count > 6 ? (m->favorite_count + 5) / 6 : 1;

        if (m->page < 0) {
            m->page = 0;
        }
        if (m->page >= pages) {
            m->page = pages - 1;
        }
    }

    if (battery_mix_read(&battery) == 0) {
        m->battery_level = battery;
    } else if (lipc_get_int("com.lab126.powerd", "battLevel", &battery) == 0 &&
               battery >= 0 && battery <= 100) {
        m->battery_level = battery;
    } else {
        m->battery_level = -1;
    }

    m->wifi_on = 0;
    m->ssid[0] = 0;
    {
        char status[1024];
        char *argv[] = { "/usr/bin/wpa_cli", "-i", "wlan0", "status", NULL };
        if (run_capture(argv, status, sizeof(status)) == 0 &&
            parse_wpa_status(status, m->ssid, sizeof(m->ssid))) {
            m->wifi_on = 1;
            if (m->ssid[0] == 0) {
                snprintf(m->ssid, sizeof(m->ssid), "ON");
            }
        }
    }
}

static int model_status_changed(const HomeModel *a, const HomeModel *b)
{
    return a->battery_level != b->battery_level || a->wifi_on != b->wifi_on ||
           strcmp(a->ssid, b->ssid) != 0;
}

static void wifi_ip(char *out, size_t cap)
{
    struct ifreq ifr;
    int fd;

    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = 0;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return;
    }
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "wlan0");
    if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
        const struct sockaddr_in *sin =
            (const struct sockaddr_in *)&ifr.ifr_addr;
        if (inet_ntop(AF_INET, &sin->sin_addr, out, (socklen_t)cap) == NULL) {
            out[0] = 0;
        }
    }
    close(fd);
}

static const char *news_root(void);

static void settings_update(void)
{
    struct stat st;
    int intensity = -1;

    settings.home = model;

    /* The screen draws a 0..16 scale over the device's 0..KHOME_FL_MAX. */
    settings.brightness = 0;
    if (lipc_get_int("com.lab126.powerd", "flIntensity", &intensity) == 0) {
        if (intensity < 0) {
            intensity = 0;
        }
        if (intensity > KHOME_FL_MAX) {
            intensity = KHOME_FL_MAX;
        }
        settings.brightness = (intensity * 16 + 12) / KHOME_FL_MAX;
    }

    /* Below 4.1 V is the limited setting; the stock 4200mV reads as off.
     * A missing node (the host build, or an unknown panel) stays unknown and
     * draws as off. */
    {
        int mv = cv_prm_read();

        settings.charge_limit = mv < 0 ? -1 : (mv < 4100 ? 1 : 0);
    }

    wifi_ip(settings.ip, sizeof(settings.ip));

    if (stat(news_root(), &st) == 0) {
        struct tm tm;
        localtime_r(&st.st_mtime, &tm);
        snprintf(settings.last_update, sizeof(settings.last_update),
                 "%s %d %02d:%02d", month_names[tm.tm_mon], tm.tm_mday,
                 tm.tm_hour, tm.tm_min);
    } else {
        snprintf(settings.last_update, sizeof(settings.last_update), "Never");
    }
    snprintf(settings.version, sizeof(settings.version),
             "Folio 1.0 (PW2)");
}

/* layout.c rounds both ends of a filled rectangle, so a partial refresh must
 * round the same two points or its far edge falls short of what was drawn. */
static void scale_layout_rect(int x, int y, int w, int h, int *px, int *py,
                              int *pw, int *ph)
{
    int x0 = (int)lroundf((double)x * (double)KHOME_SCALE);
    int y0 = (int)lroundf((double)y * (double)KHOME_SCALE);
    int x1 = (int)lroundf((double)(x + w) * (double)KHOME_SCALE);
    int y1 = (int)lroundf((double)(y + h) * (double)KHOME_SCALE);

    *px = x0;
    *py = y0;
    *pw = x1 - x0;
    *ph = y1 - y0;
}

static const char *favorite_name(void)
{
    if (favorite_index < 0 || favorite_index >= model.favorite_count) {
        return "";
    }
    return model.favorites[favorite_index].name;
}

/* The canvas is always the portrait 758x1024 screen.  A landscape browser
 * leaves fb0 at 1024x758 until winmgr unloads it; drawing into that geometry
 * would use the wrong stride, so every partial draw asks first. */
static int fb_matches_canvas(void)
{
    return fb_refresh_geometry(&fb) == 0 && fb.width == canvas.w &&
           fb.height == canvas.h;
}

static void draw_frame_ex(int full, int reread)
{
    if (!fb_matches_canvas()) {
        log_msg("fb: %dx%d while the canvas is %dx%d, not drawing",
                fb.width, fb.height, canvas.w, canvas.h);
        return;
    }
    canvas_clear(&canvas, 255);
    if (state == KHOME_STATE_SETTINGS_CONFIRM ||
        state == KHOME_STATE_SETTINGS) {
        if (reread) {
            settings_update();
        }
        draw_settings(&canvas, &fonts, &icons, &settings, KHOME_SCALE);
        if (state == KHOME_STATE_SETTINGS_CONFIRM) {
            draw_confirm(&canvas, &fonts, confirm, KHOME_SCALE);
        }
    } else if (state == KHOME_STATE_HOME_CONFIRM) {
        draw_home(&canvas, &fonts, &icons, &model, KHOME_SCALE);
        draw_confirm_favorite(&canvas, &fonts, favorite_name(), KHOME_SCALE);
    } else {
        draw_home(&canvas, &fonts, &icons, &model, KHOME_SCALE);
    }
    if (fb_blit(&fb, &canvas) < 0) {
        log_msg("fb_blit failed");
        return;
    }
    if (full) {
        fb_refresh_full(&fb);
    } else {
        int x, y, w, h;
        int px, py, pw, ph;
        layout_clock_rect(&x, &y, &w, &h);
        scale_layout_rect(x, y, w, h, &px, &py, &pw, &ph);
        fb_refresh_rect(&fb, px, py, pw, ph);
    }
}

static void draw_frame(int full)
{
    draw_frame_ex(full, 1);
}

static void settings_wifi_toggle(void)
{
    int target = !settings.home.wifi_on;
    int value = target ? 1 : 0;

    log_msg("settings: wifi enable %d", value);
    if (lipc_set_int("com.lab126.wifid", "enable", value) < 0) {
        log_msg("settings: wifi enable failed");
        return;
    }
    /* Show the target at once; the lipc side takes a few seconds to settle,
     * so a short poll below catches the real state up.  model carries the
     * same value so that poll notices a device that refused the change. */
    settings.home.wifi_on = target;
    model.wifi_on = target;
    draw_frame_ex(1, 0);
    wifi_poll_left = KHOME_WIFI_POLL_COUNT;
    wifi_poll_due = now_ms() + KHOME_WIFI_POLL_MS;
}

static int write_atomic(const char *tag, const char *path, const char *text);

static void settings_charge_limit_toggle(void)
{
    int target = settings.charge_limit != 1;
    const char *mv = target ? "4000" : "4200";
    char text[8];

    /* Apply the voltage first: a rejected write leaves the toggle as it was. */
    if (cv_prm_write(mv) < 0) {
        return;
    }
    snprintf(text, sizeof(text), "%s\n", target ? "on" : "off");
    write_atomic("charge-limit", KHOME_CHARGE_LIMIT_FILE, text);
    settings.charge_limit = target;
    log_msg("settings: charge limit %s (%s mV)", target ? "on" : "off", mv);
    draw_frame_ex(1, 0);
}

static void settings_brightness_step(int delta)
{
    int next = settings.brightness + delta;
    int value;

    if (next < 0) {
        next = 0;
    }
    if (next > 16) {
        next = 16;
    }
    if (next == settings.brightness) {
        log_msg("settings: frontlight already at %d", next);
        return;
    }
    /* Rounds so settings_update reads the same step back. */
    value = (next * KHOME_FL_MAX + 8) / 16;
    log_msg("settings: flIntensity %d", value);
    if (lipc_set_int("com.lab126.powerd", "flIntensity", value) < 0) {
        log_msg("settings: flIntensity failed");
        return;
    }
    settings.brightness = next;
    draw_frame_ex(1, 0);
}

static void handle_event(KhomeEvent ev);
static void browser_launch_abort(const char *why, const char *notice);
static void browser_orientation_toggle(void);
static int watch_retry_due(int64_t *last);
static void shield_start(int height);
static void shield_stop(void);
static void shield_sync(void);
static void shield_old_stop(int keep);
static void shield_old_clear(void);
static void shield_old_tick(void);
static void shield_expose_band(int top, int bottom);
static void helper_stop(pid_t *pid, const char *name);

/* The text edition fetch.sh installs under text/current.  khome also accepts
 * KHOME_NEWS_TEXT_DIR so the host renderer can point at a fixture tree. */
static const char *news_root(void)
{
    const char *env = getenv("KHOME_NEWS_TEXT_DIR");

    return env != NULL && env[0] != 0 ? env : KHOME_NEWS_TEXT_DIR;
}

static void news_text_size_load(void)
{
    FILE *f = fopen(KHOME_NEWS_TEXT_SIZE_FILE, "r");
    int value = 0;

    if (f == NULL) {
        return;
    }
    if (fscanf(f, "%d", &value) == 1 &&
        (value == 26 || value == 30 || value == 36)) {
        news_text_size = value;
    }
    fclose(f);
}

static void news_text_size_save(void)
{
    char text[16];

    snprintf(text, sizeof(text), "%d\n", news_text_size);
    write_atomic("news", KHOME_NEWS_TEXT_SIZE_FILE, text);
}

static void news_reload(void)
{
    kt_article_close(&news_article);
    if (!kt_paper_load(&news_paper, news_root())) {
        news_have_paper = 0;
        news_page = 0;
        log_msg("news: no text edition in %s", news_root());
        return;
    }
    kt_paper_build(&news_paper, &fonts);
    news_have_paper = 1;
    if (news_page < 0) {
        news_page = 0;
    }
    if (news_page >= news_paper.layout.page_count) {
        news_page = news_paper.layout.page_count - 1;
    }
    log_msg("news: text edition %s, %d stories, %d pages",
            news_paper.edition.id, news_paper.story_count,
            news_paper.layout.page_count);
}

/* A fetch left running by a previous khome is still a fetch.  Looking at
 * /proc keeps a restart from starting a second one against the same cache. */
static int news_fetch_running(void)
{
    DIR *dir;
    struct dirent *ent;
    int found = 0;

    dir = opendir("/proc");
    if (dir == NULL) {
        return 0;
    }
    while ((ent = readdir(dir)) != NULL && !found) {
        char path[64];
        char cmd[1024];
        ssize_t got;
        int fd;
        int i;

        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') {
            continue;
        }
        snprintf(path, sizeof(path), "/proc/%s/cmdline", ent->d_name);
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            continue;
        }
        got = read(fd, cmd, sizeof(cmd) - 1);
        close(fd);
        if (got <= 0) {
            continue;
        }
        cmd[got] = 0;
        for (i = 0; i < got - 1; i++) {
            if (cmd[i] == 0) {
                cmd[i] = ' ';
            }
        }
        if (strstr(cmd, "newspaper/bin/fetch.sh") != NULL) {
            found = 1;
        }
    }
    closedir(dir);
    return found;
}

static void news_signature(char *out, size_t cap)
{
    struct stat st;

    if (out == NULL || cap == 0) {
        return;
    }
    if (stat(news_root(), &st) != 0) {
        snprintf(out, cap, "none");
        return;
    }
    snprintf(out, cap, "%llu:%lld:%lld", (unsigned long long)st.st_ino,
             (long long)st.st_mtime, (long long)st.st_size);
}

/* Identifies the edition on screen.  fetch.sh rotates text/current whole, so
 * the directory stat changes with the content and edition.tsv carries the id
 * that names it in the log. */
static void news_edition_signature(char *out, size_t cap)
{
    struct stat st;
    char id[32] = "";
    char path[512];
    FILE *f;

    if (out == NULL || cap == 0) {
        return;
    }
    snprintf(path, sizeof(path), "%s/edition.tsv", news_root());
    f = fopen(path, "r");
    if (f != NULL) {
        char line[128];
        if (fgets(line, sizeof(line), f) != NULL) {
            char *tab = strchr(line, '\t');
            if (tab != NULL) {
                char *end = tab + 1;
                size_t len = strlen(end);
                while (len > 0 && (end[len - 1] == '\n' || end[len - 1] == '\r')) {
                    end[--len] = 0;
                }
                snprintf(id, sizeof(id), "%s", end);
            }
        }
        fclose(f);
    }
    if (stat(news_root(), &st) == 0) {
        snprintf(out, cap, "%s:%llu:%lld", id,
                 (unsigned long long)st.st_ino, (long long)st.st_mtime);
        return;
    }
    snprintf(out, cap, "%s:none", id);
}

/* Returns 1 and forgets the article when the edition under it was replaced. */
static int news_edition_update(void)
{
    char current[128];

    news_edition_signature(current, sizeof(current));
    if (strcmp(current, news_edition) == 0) {
        return 0;
    }
    if (news_view == NEWS_VIEW_ARTICLE) {
        news_view = NEWS_VIEW_PAGE;
        news_page = 0;
        kt_article_close(&news_article);
        log_msg("news: edition changed, back to page 1");
    }
    news_have_paper = 0;
    snprintf(news_edition, sizeof(news_edition), "%s", current);
    return 1;
}

/* A page flip uses GL16 so the panel does not flash; the first page and every
 * fifth one take a full GC16 pass to clear the accumulated ghosting. */
static void news_draw(void)
{
    int index = news_view == NEWS_VIEW_ARTICLE ? news_article.page : news_page;
    int gc16 = index == 0 || (index + 1) % 5 == 0;

    canvas_clear(&canvas, 255);
    news_drawn_paper = 0;
    if (news_view == NEWS_VIEW_ARTICLE) {
        int next_story = news_article.story + 1;
        if (next_story >= news_paper.story_count) {
            next_story = -1;
        }
        if (news_article.raw != NULL) {
            kt_draw_article(&canvas, &fonts, &news_paper, &news_article,
                            news_article.page, next_story, 1.0f);
            news_drawn_paper = 1;
        }
    } else if (news_have_paper) {
        kt_draw_paper(&canvas, &fonts, &news_paper, news_page, 1.0f);
        news_drawn_paper = 1;
    }
    if (!news_drawn_paper) {
        draw_news_empty(&canvas, &fonts, &icons, &model, KHOME_SCALE);
    }
    if (fb_blit(&fb, &canvas) < 0) {
        log_msg("news: fb_blit failed");
        return;
    }
    if (fb_refresh_full_wave(&fb, news_drawn_paper && !gc16 ? "GL16" : "GC16") <
        0) {
        log_msg("news: refresh failed");
    }
}

/* Only the empty News screen draws the status bar and the masthead date; the
 * text paper and an article carry their own. */
static void news_status_draw(void)
{
    int px;
    int py;
    int pw;
    int ph;

    if (news_drawn_paper) {
        return;
    }
    canvas_clear(&canvas, 255);
    draw_news_empty(&canvas, &fonts, &icons, &model, KHOME_SCALE);
    scale_layout_rect(0, 0, 1072, 192, &px, &py, &pw, &ph);
    if (fb_blit_rect(&fb, &canvas, px, py, pw, ph) < 0) {
        log_msg("news: status fb_blit_rect failed");
        return;
    }
    if (fb_refresh_rect(&fb, px, py, pw, ph) < 0) {
        log_msg("news: status refresh failed");
    }
}

static void news_open_story(int story)
{
    if (story < 0 || story >= news_paper.story_count) {
        return;
    }
    if (news_view != NEWS_VIEW_ARTICLE) {
        news_origin_page = news_page;
    }
    if (!kt_article_open(&news_article, &news_paper, &fonts, story,
                         news_text_size)) {
        log_msg("news: cannot read article %s",
                news_paper.stories[story].id);
        return;
    }
    news_view = NEWS_VIEW_ARTICLE;
    log_msg("news: article %s 1/%d from page %d",
            news_paper.stories[story].id, news_article.page_count,
            news_origin_page + 1);
    news_draw();
}

static void news_show_page(int page)
{
    news_view = NEWS_VIEW_PAGE;
    kt_article_close(&news_article);
    if (news_have_paper) {
        if (page < 0) {
            page = 0;
        }
        if (page >= news_paper.layout.page_count) {
            page = news_paper.layout.page_count - 1;
        }
        news_page = page;
    }
    news_draw();
}

static void news_back_to_paper(void)
{
    if (news_view == NEWS_VIEW_ARTICLE) {
        news_show_page(news_origin_page);
    }
}

static void news_page_step(int delta)
{
    int next;

    if (news_view == NEWS_VIEW_ARTICLE) {
        next = news_article.page + delta;
        if (next < 0 || next >= news_article.page_count) {
            log_msg("news: article %s %d/%d -> page %d",
                    news_paper.stories[news_article.story].id,
                    news_article.page + 1, news_article.page_count,
                    news_origin_page + 1);
            news_show_page(news_origin_page);
            return;
        }
        if (next == news_article.page) {
            return;
        }
        news_article.page = next;
        log_msg("news: article %s %d/%d",
                news_paper.stories[news_article.story].id,
                news_article.page + 1, news_article.page_count);
        news_draw();
        return;
    }
    if (!news_have_paper || news_paper.layout.page_count <= 0) {
        return;
    }
    next = news_page + delta;
    if (next < 0) {
        next = 0;
    }
    if (next >= news_paper.layout.page_count) {
        next = news_paper.layout.page_count - 1;
    }
    if (next == news_page) {
        return;
    }
    news_page = next;
    log_msg("news: page %d/%d", news_page + 1,
            news_paper.layout.page_count);
    news_draw();
}

static void news_text_size_cycle(void)
{
    int index = news_text_size == 26 ? 0 : (news_text_size == 36 ? 2 : 1);

    news_text_size = index == 0 ? 30 : (index == 1 ? 36 : 26);
    news_text_size_save();
    if (news_view == NEWS_VIEW_ARTICLE && news_article.raw != NULL) {
        kt_article_set_size(&news_article, &news_paper, &fonts,
                            news_text_size);
        log_msg("news: text size %d", news_text_size);
        news_draw();
    }
}

static void news_menu_model(NewsMenuModel *m)
{
    memset(m, 0, sizeof(*m));
    m->hour = model.hour;
    m->minute = model.minute;
    m->battery_level = model.battery_level;
    m->article = news_view == NEWS_VIEW_ARTICLE;
    snprintf(m->notice, sizeof(m->notice), "%s", news_menu_notice);
    if (m->article) {
        m->position = news_article.page + 1;
        m->count = news_article.page_count;
    } else {
        m->position = news_have_paper ? news_page + 1 : 0;
        m->count = news_have_paper ? news_paper.layout.page_count : 0;
    }
}

static void news_menu_draw(void)
{
    NewsMenuModel m;
    int height;

    news_menu_model(&m);
    canvas_clear(&canvas, 255);
    draw_news_menu(&canvas, &fonts, &icons, &m, KHOME_SCALE);
    height = (int)lroundf((float)layout_news_menu_height() * KHOME_SCALE);
    if (fb_blit_rect(&fb, &canvas, 0, 0, fb.width, height) < 0) {
        log_msg("news menu: fb_blit_rect failed");
        return;
    }
    if (fb_refresh_rect(&fb, 0, 0, fb.width, height) < 0) {
        log_msg("news menu: refresh failed");
    }
}

static void news_menu_status_draw(void)
{
    NewsMenuModel m;
    int px;
    int py;
    int pw;
    int ph;

    news_menu_model(&m);
    draw_news_menu_status(&canvas, &fonts, &icons, &m, KHOME_SCALE);
    scale_layout_rect(0, 0, 1072, 48, &px, &py, &pw, &ph);
    if (fb_blit_rect(&fb, &canvas, px, py, pw, ph) < 0) {
        log_msg("news menu: status fb_blit_rect failed");
        return;
    }
    if (fb_refresh_rect(&fb, px, py, pw, ph) < 0) {
        log_msg("news menu: status refresh failed");
    }
}

static void news_fetch_notice(NewsFetchSource source, const char *text)
{
    if (source == NEWS_FETCH_FROM_SETTINGS) {
        snprintf(settings.notice, sizeof(settings.notice), "%s", text);
        draw_frame_ex(1, 0);
    } else if (source == NEWS_FETCH_FROM_MENU) {
        snprintf(news_menu_notice, sizeof(news_menu_notice), "%s", text);
    }
}

/* Returns 1 when a fetch is running or already was, so the menu may close.
 * Returns 0 when nothing started; the menu keeps its notice on screen. */
static int news_fetch_start(NewsFetchSource source)
{
    int from_settings = source == NEWS_FETCH_FROM_SETTINGS;
    int wifi = from_settings ? settings.home.wifi_on : model.wifi_on;
    pid_t pid;

    if (fetch_pid > 0) {
        log_msg("news: fetch already running as pid %d", (int)fetch_pid);
        return 1;
    }
    if (news_fetch_running()) {
        log_msg("news: another fetch.sh is still running, not starting one");
        return 1;
    }
    if (!wifi) {
        log_msg("news: Wi-Fi is off, not fetching");
        news_fetch_notice(source, "Turn on Wi-Fi first");
        return 0;
    }
    if (access(KHOME_NEWS_FETCH, R_OK) != 0) {
        log_msg("news: %s is not readable: %s", KHOME_NEWS_FETCH,
                strerror(errno));
        news_fetch_notice(source, "Update failed (127)");
        return 0;
    }
    news_signature(fetch_signature, sizeof(fetch_signature));
    pid = fork();
    if (pid < 0) {
        log_msg("news: fork failed: %s", strerror(errno));
        news_fetch_notice(source, "Update failed (127)");
        return 0;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);

        /* The wrapper's flock, the grabbed touch device, the framebuffer and
         * the LIPC watches must not outlive khome in an orphaned fetch: an
         * inherited event1 handle would keep the touchscreen grabbed. */
        close(9);
        if (touch.fd >= 0) {
            close(touch.fd);
        }
        if (fb.fd >= 0) {
            close(fb.fd);
        }
        if (sig_pipe[0] >= 0) {
            close(sig_pipe[0]);
        }
        if (sig_pipe[1] >= 0) {
            close(sig_pipe[1]);
        }
        if (power_watch.fd >= 0) {
            close(power_watch.fd);
        }
        if (app_watch.fd >= 0) {
            close(app_watch.fd);
        }
        if (log_file != NULL) {
            close(fileno(log_file));
        }
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) {
                close(devnull);
            }
        }
        setenv("NP_KEEP_WIFI", "1", 1);
        setenv("PATH", KHOME_TOOL_PATH, 1);
        execl("/bin/sh", "sh", KHOME_NEWS_FETCH, "--quiet", (char *)NULL);
        _exit(127);
    }
    fetch_pid = pid;
    settings.updating = 1;
    settings.notice[0] = 0;
    log_msg("news: fetch started pid %d", (int)pid);
    if (from_settings) {
        draw_frame_ex(1, 0);
    }
    return 1;
}

static void news_fetch_done(int status)
{
    char signature[64];
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    int changed;
    int edition_changed;

    fetch_pid = 0;
    settings.updating = 0;
    news_signature(signature, sizeof(signature));
    changed = strcmp(signature, fetch_signature) != 0;
    edition_changed = news_edition_update();
    log_msg("news: fetch exited (%d), content %s, edition %s", code,
            changed ? "changed" : "unchanged",
            edition_changed ? "changed" : "unchanged");
    if (state == KHOME_STATE_NEWS || state == KHOME_STATE_NEWS_MENU) {
        if (edition_changed || (code == 0 && changed)) {
            handle_event(KHOME_EV_NEWS_FETCHED);
        }
        return;
    }
    if (state == KHOME_STATE_SETTINGS ||
        state == KHOME_STATE_SETTINGS_CONFIRM) {
        if (code != 0) {
            snprintf(settings.notice, sizeof(settings.notice),
                     "Update failed (%d)", code);
        } else {
            settings.notice[0] = 0;
        }
        handle_event(KHOME_EV_NEWS_FETCHED);
    }
}

/* The input core keeps the last MT position per slot and drops an ABS event
 * equal to it.  Linux 3.0 does not expose that per-slot value through
 * EVIOCGABS, so after a restart or another app's touch, the first press at the
 * same point can arrive without X/Y.  While we own the device, put the slot
 * back at 0,0 and drain our own events; a genuine 0,0 press is then correct
 * too because the gesture starts there. */
static void reset_touch_position(void)
{
    struct input_event ev[4];
    int fd;

    memset(ev, 0, sizeof(ev));
    ev[0].type = EV_ABS;
    ev[0].code = ABS_MT_SLOT;
    ev[0].value = 0;
    ev[1].type = EV_ABS;
    ev[1].code = ABS_MT_POSITION_X;
    ev[1].value = 0;
    ev[2].type = EV_ABS;
    ev[2].code = ABS_MT_POSITION_Y;
    ev[2].value = 0;
    ev[3].type = EV_SYN;
    ev[3].code = SYN_REPORT;
    ev[3].value = 0;

    fd = open(touch.path, O_WRONLY);
    if (fd < 0) {
        log_msg("touch: cannot open %s to reset position: %s", touch.path,
                strerror(errno));
        return;
    }
    if (write(fd, ev, sizeof(ev)) != (ssize_t)sizeof(ev)) {
        log_msg("touch: short write resetting position");
        close(fd);
        return;
    }
    close(fd);
    log_msg("touch: reset position 0,0");
}

static void grab_touch(int on)
{
    on = on ? 1 : 0;
    if (on == touch_grabbed) {
        return;
    }
    if (input_grab(&touch, on) == 0) {
        touch_grabbed = on;
        log_msg("touch: EVIOCGRAB %d", on);
    } else {
        log_msg("touch: EVIOCGRAB %d failed", on);
    }
    /* Events queued while the other side owned the device must not become a
     * gesture for the state we just entered.  Their positions still matter:
     * evdev drops an ABS value identical to the previous one, so a zeroed
     * position makes the next press at the same x or y arrive without that
     * axis and tap at 0. */
    input_drain(&touch, &gesture);
    gesture_cancel(&gesture);
    if (touch_grabbed) {
        reset_touch_position();
        input_drain(&touch, &gesture);
        /* The injected 0,0 may be filtered when the slot is already there. */
        gesture_set_position(&gesture, 0, 0);
        gesture_cancel(&gesture);
    }
}

/* Writes one tap as the kernel's evdev events, the same sequence
 * scripts/dev/tapgen.py produces.  The input core drops ABS values identical
 * to the previous ones, so successive taps get a 1 px jitter; without it the
 * second alert dismissal would arrive as (0,0). */
static void inject_tap(int x, int y)
{
    static int tap_seq;
    struct input_event down[6];
    struct input_event up[4];
    int fd;

    tap_seq++;
    if (tap_seq > 1) {
        x += tap_seq % 3 - 1;
        y += (tap_seq / 3) % 3 - 1;
    }

    memset(down, 0, sizeof(down));
    down[0].type = EV_ABS;
    down[0].code = ABS_MT_TOOL_TYPE;
    down[0].value = 0;
    down[1].type = EV_ABS;
    down[1].code = ABS_MT_TRACKING_ID;
    down[1].value = 77;
    down[2].type = EV_ABS;
    down[2].code = ABS_MT_POSITION_X;
    down[2].value = x;
    down[3].type = EV_ABS;
    down[3].code = ABS_MT_POSITION_Y;
    down[3].value = y;
    down[4].type = EV_KEY;
    down[4].code = BTN_TOUCH;
    down[4].value = 1;
    down[5].type = EV_SYN;
    down[5].code = SYN_REPORT;
    down[5].value = 0;

    memset(up, 0, sizeof(up));
    up[0].type = EV_ABS;
    up[0].code = ABS_MT_TOOL_TYPE;
    up[0].value = 0;
    up[1].type = EV_ABS;
    up[1].code = ABS_MT_TRACKING_ID;
    up[1].value = -1;
    up[2].type = EV_KEY;
    up[2].code = BTN_TOUCH;
    up[2].value = 0;
    up[3].type = EV_SYN;
    up[3].code = SYN_REPORT;
    up[3].value = 0;

    fd = open(touch.path, O_WRONLY);
    if (fd < 0) {
        log_msg("touch: cannot open %s for injection: %s", touch.path,
                strerror(errno));
        return;
    }
    if (write(fd, down, sizeof(down)) != (ssize_t)sizeof(down)) {
        log_msg("touch: short write for press");
    }
    sleep_ms(120);
    if (write(fd, up, sizeof(up)) != (ssize_t)sizeof(up)) {
        log_msg("touch: short write for release");
    }
    close(fd);
    log_msg("touch: injected tap %d,%d", x, y);
}

static int read_tabs(TabList *tabs)
{
    static char text[KHOME_MAX_TABS * KHOME_TAB_URL_MAX + 64];
    FILE *fp;
    size_t got;
    int before = tabs->count;

    fp = fopen(KHOME_TABS_FILE, "r");
    if (fp == NULL) {
        tabs_init(tabs);
        return 0;
    }
    got = fread(text, 1, sizeof(text) - 1, fp);
    /* A read that stops short of EOF or fails leaves no usable list.
     * Parsing it would cut the tabs off and the next write would drop them
     * from the file, so keep the caller's list instead. */
    if (ferror(fp) || !feof(fp)) {
        log_msg("tabs: short read of %s, keeping %d entries", KHOME_TABS_FILE,
                before);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    text[got] = 0;
    return tabs_parse(tabs, text);
}

/* Writes text to path through a temporary file so a reader never sees half a
 * list and a crash leaves the old one in place. */
static int write_atomic(const char *tag, const char *path, const char *text)
{
    char tmp[256];
    FILE *fp;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    fp = fopen(tmp, "w");
    if (fp == NULL) {
        log_msg("%s: cannot write %s: %s", tag, tmp, strerror(errno));
        return -1;
    }
    if (fputs(text, fp) == EOF || fflush(fp) != 0) {
        log_msg("%s: write failed: %s", tag, strerror(errno));
        fclose(fp);
        remove(tmp);
        return -1;
    }
    if (fsync(fileno(fp)) != 0) {
        log_msg("%s: write failed: %s", tag, strerror(errno));
        fclose(fp);
        remove(tmp);
        return -1;
    }
    fclose(fp);
    if (rename(tmp, path) != 0) {
        log_msg("%s: rename failed: %s", tag, strerror(errno));
        remove(tmp);
        return -1;
    }
    {
        int dirfd = open(KHOME_DIR, O_RDONLY);
        if (dirfd >= 0) {
            fsync(dirfd);
            close(dirfd);
        }
    }
    return 0;
}

static void write_tabs(void)
{
    static char text[KHOME_MAX_TABS * KHOME_TAB_URL_MAX + 64];

    if (tabs_format(&tabs, text, sizeof(text)) < 0) {
        log_msg("tabs: cannot format the list");
        return;
    }
    write_atomic("tabs", KHOME_TABS_FILE, text);
}

/* Favorites live in the same directory as tabs.txt.  The file is read on
 * every entry to HOME so the sheet's edits show up without a restart. */
static void read_favorites(HomeModel *m)
{
    FILE *fp;
    size_t got = 0;
    int before = m->favorite_count;

    favorites_text[0] = 0;
    fp = fopen(KHOME_FAVORITES_FILE, "r");
    if (fp == NULL) {
        if (errno != ENOENT) {
            log_msg("favorites: cannot read %s: %s", KHOME_FAVORITES_FILE,
                    strerror(errno));
            return;
        }
    } else {
        got = fread(favorites_text, 1, sizeof(favorites_text) - 1, fp);
        /* A read that stops short of EOF or fails leaves no usable list.
         * Parsing it would cut the entries off and the next write would
         * drop them from the file, so keep the in-memory list instead. */
        if (ferror(fp) || !feof(fp)) {
            log_msg("favorites: short read of %s, keeping %d entries",
                    KHOME_FAVORITES_FILE, before);
            fclose(fp);
            return;
        }
        fclose(fp);
        favorites_text[got] = 0;
    }
    m->favorite_count =
        favorites_parse(m->favorites, KHOME_MAX_FAVORITES, favorites_text);
    if (m->favorite_count != before) {
        log_msg("favorites: %d entries", m->favorite_count);
    }
}

static void write_favorites(void)
{
    if (favorites_format(model.favorites, model.favorite_count, favorites_text,
                         sizeof(favorites_text)) < 0) {
        log_msg("favorites: cannot format the list");
        return;
    }
    write_atomic("favorites", KHOME_FAVORITES_FILE, favorites_text);
}

static int favorite_saved(const char *url)
{
    return favorites_find(model.favorites, model.favorite_count, url) >= 0;
}

/* Shared by the sheet's Favorited button and the home tile long press. */
static void remove_favorite_at(int index)
{
    if (index < 0 || index >= model.favorite_count) {
        log_msg("favorites: no entry %d", index);
        return;
    }
    log_msg("favorites: removed %s", model.favorites[index].url);
    favorites_remove(model.favorites, &model.favorite_count, index);
    write_favorites();
}

static void open_url(const char *url)
{
    char encoded[KHOME_ENCODED_MAX];
    char uri[sizeof("app://com.lab126.browser?view=") + KHOME_ENCODED_MAX];

    if (tabs_encode_component(url, encoded, sizeof(encoded)) < 0) {
        log_msg("browser: URL too long: %s", url);
        return;
    }
    snprintf(uri, sizeof(uri), "app://com.lab126.browser?view=%s", encoded);
    log_msg("browser: opening %s", url);
    /* The browser only writes lastUrl once the page loads, so a DB mtime at
     * or before this moment still describes the previous page. */
    clock_gettime(CLOCK_REALTIME, &last_open_at);
    last_open_set = 1;
    if (lipc_set_str("com.lab126.appmgrd", "start", uri) == 0) {
        log_msg("browser: start requested");
    } else {
        log_msg("browser: start failed");
    }
}

/* The browser exposes no LIPC URL property in this firmware.  It records the
 * page it is showing in its LocalStorage: lastUrl is the current URL and the
 * newest history entry for that URL carries the title, both UTF-16LE, so the
 * query prints them as hex.  lastUrl is one value for the whole browser, so it
 * is only trusted when the DB was written after khome's last open_url. */
static int browser_capture_url(void)
{
    static char query[] =
        "select hex(u), hex(coalesce(t,'')) from (select json_extract((select "
        "value from ItemTable where key='lastUrl'),'$') u, (select "
        "json_extract(j.value,'$.name') from ItemTable, json_each(ItemTable."
        "value) j where ItemTable.key='history' and json_extract(j.value,"
        "'$.url')=(select json_extract(value,'$') from ItemTable where "
        "key='lastUrl') order by j.key desc limit 1) t);";
    char *argv[] = { KHOME_SQLITE3, "-readonly", "-cmd", ".timeout 200",
                     "-separator", " ", KHOME_BROWSER_DB, query, NULL };
    char row[KHOME_BROWSER_ROW_MAX];
    char url[KHOME_TAB_URL_MAX];
    char title[KHOME_PAGE_TITLE_MAX];
    const char *current;
    struct stat st;
    int active;

    if (!last_open_set) {
        log_msg("browser: DB URL rejected: no tab opened yet");
        return 0;
    }
    if (stat(KHOME_BROWSER_DB, &st) != 0) {
        log_msg("browser: DB URL rejected: stat failed: %s", strerror(errno));
        return 0;
    }
    if (st.st_mtim.tv_sec < last_open_at.tv_sec ||
        (st.st_mtim.tv_sec == last_open_at.tv_sec &&
         st.st_mtim.tv_nsec <= last_open_at.tv_nsec)) {
        log_msg("browser: DB URL rejected: mtime is not newer than the open");
        return 0;
    }
    if (run_capture(argv, row, sizeof(row)) < 0) {
        log_msg("browser: DB URL rejected: sqlite3 failed");
        return 0;
    }
    if (tabs_browser_row_parse(row, url, sizeof(url), title, sizeof(title)) <
        0) {
        log_msg("browser: DB URL rejected: bad row");
        return 0;
    }
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        log_msg("browser: DB URL rejected: not http(s): %s", url);
        return 0;
    }
    active = tabs.active;
    if (active < 0 || active >= tabs.count) {
        log_msg("browser: DB URL rejected: no active tab: %s", url);
        return 0;
    }
    current = tabs.tabs[active].url;
    if (strcmp(current, url) != 0) {
        if (tabs_set_url(&tabs, active, url) < 0) {
            log_msg("browser: DB URL rejected: tab cannot hold %s", url);
            return 0;
        }
        write_tabs();
    }
    memcpy(page_title_url, url, strlen(url) + 1);
    memcpy(page_title, title, strlen(title) + 1);
    log_msg("browser: current URL from LocalStorage: %s", url);
    return 1;
}

static void trim_ascii_space(char *s)
{
    size_t len;

    if (s == NULL) {
        return;
    }
    len = strlen(s);
    while (len > 0) {
        char c = s[len - 1];

        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        s[--len] = 0;
    }
}

/* Reads the browser settings row without letting sqlite3 convert the UTF-16LE
 * bytes: hex() is decoded in C, the same way browser_capture_url() reads the
 * URL and history.  valid is json_valid(value), so a half-written object can
 * never be mistaken for a usable settings value. */
static int browser_settings_read(char *json, size_t json_cap, char *hex,
                                 size_t hex_cap, int *valid)
{
    static char query[] =
        "select json_valid(value), hex(value) from ItemTable "
        "where key='settings';";
    char *argv[] = { KHOME_SQLITE3, "-readonly", "-cmd", ".timeout 200",
                     "-separator", " ", KHOME_BROWSER_DB, query, NULL };
    char row[KHOME_BROWSER_SETTINGS_ROW_MAX];
    char *sep;
    char *hex_value;

    if (json == NULL || json_cap == 0 || valid == NULL) {
        return -1;
    }
    json[0] = 0;
    *valid = 0;
    if (hex != NULL && hex_cap > 0) {
        hex[0] = 0;
    }
    if (run_capture(argv, row, sizeof(row)) < 0) {
        return -1;
    }
    trim_ascii_space(row);
    sep = strchr(row, ' ');
    if (sep == NULL) {
        return -1;
    }
    *sep = 0;
    *valid = strcmp(row, "1") == 0;
    hex_value = sep + 1;
    trim_ascii_space(hex_value);
    if (hex_value[0] == 0) {
        return -1;
    }
    if (hex != NULL && hex_cap > 0) {
        if (strlen(hex_value) >= hex_cap) {
            return -1;
        }
        snprintf(hex, hex_cap, "%s", hex_value);
    }
    if (tabs_utf16_hex_decode(hex_value, json, json_cap) < 0) {
        *valid = 0;
        return -1;
    }
    return 0;
}

/* The SQL is fixed: the orientation value is the only thing that changes, and
 * json_set leaves every other key in the object alone.  json_valid guards
 * against writing through a settings value khome did not understand. */
static int browser_settings_write_orientation(int landscape)
{
    static char landscape_sql[] =
        "update ItemTable set value=json_set(value,'$.orientation',"
        "'landscapeLeft') where key='settings' and json_valid(value);";
    static char portrait_sql[] =
        "update ItemTable set value=json_set(value,'$.orientation','auto') "
        "where key='settings' and json_valid(value);";
    char out[64];
    char *argv[] = { KHOME_SQLITE3, "-cmd", ".timeout 200", KHOME_BROWSER_DB,
                     landscape ? landscape_sql : portrait_sql, NULL };

    return run_capture(argv, out, sizeof(out));
}

/* Restores the exact bytes read before the write.  Hex contains only [0-9A-F]
 * so no shell quoting, backslash, or JSON escape can corrupt the value. */
static int browser_settings_restore(const char *hex)
{
    char sql[KHOME_BROWSER_SETTINGS_HEX_MAX + 128];
    char out[64];
    char *argv[] = { KHOME_SQLITE3, "-cmd", ".timeout 200", KHOME_BROWSER_DB,
                     sql, NULL };
    const char *p;

    if (hex == NULL || hex[0] == 0) {
        return -1;
    }
    for (p = hex; *p != 0; p++) {
        if (!isxdigit((unsigned char)*p)) {
            return -1;
        }
    }
    snprintf(sql, sizeof(sql),
             "update ItemTable set value=CAST(X'%s' AS TEXT) "
             "where key='settings';", hex);
    return run_capture(argv, out, sizeof(out));
}

static int browser_orientation_refresh(void)
{
    char json[KHOME_BROWSER_SETTINGS_MAX];
    int valid = 0;
    int landscape;

    if (browser_settings_read(json, sizeof(json), NULL, 0, &valid) < 0 ||
        !valid) {
        return -1;
    }
    landscape = tabs_browser_orientation(json) ==
                BROWSER_ORIENTATION_LANDSCAPE;
    if (landscape != model.browser_landscape) {
        log_msg("browser: settings orientation is %s",
                landscape ? "landscapeLeft" : "portrait");
        model.browser_landscape = landscape;
    }
    return 0;
}

static int sheet_height_px(void)
{
    /* The shield is mapped before draw_sheet() runs, so the height has to come
     * from the tab list rather than from the model draw_sheet() fills in. */
    TabSheetModel m = sheet;

    m.tabs = tabs;
    return (int)lroundf((float)layout_tab_sheet_height(&m) * KHOME_SCALE);
}

/* The sheet and the editor are the two screens khome paints over the browser
 * without X knowing, so both need the shield.  It comes down before the
 * actions run: xexpose has to see the browser exposed, not the shield. */
static int shield_target_height(KhomeState s)
{
    /* The shield is an X window placed in portrait coordinates.  A landscape
     * framebuffer would put it over the wrong part of the page and hide the
     * browser without anything khome can draw underneath. */
    if (!fb_matches_canvas()) {
        return 0;
    }
    if (s == KHOME_STATE_SHEET) {
        return sheet_height_px();
    }
    if (s == KHOME_STATE_EDIT) {
        return fb.height;
    }
    return 0;
}

/* Brings the shield in line with state.  Closing a background tab shrinks the
 * sheet, so the height can change without the state leaving SHEET.  The old
 * shield stays up until the new one has mapped: in between, the browser would
 * repaint the whole sheet over khome's pixels.  Stopping it is deferred to the
 * poll loop so a tap during EDIT -> SHEET is not held up behind the overlap. */
static void shield_sync(void)
{
    int want = shield_target_height(state);
    pid_t old;
    int old_height;
    int keep;

    if (want == 0) {
        shield_stop();
        return;
    }
    if (shield_pid > 0 && shield_height == want) {
        return;
    }
    old = shield_pid;
    old_height = shield_height;
    shield_pid = 0;
    shield_start(want);
    if (shield_old_pid > 0) {
        /* The new shield already covers the older one, so it can go now.  Any
         * band it covered above the shields still mapped is exposed here. */
        keep = shield_pid > 0 ? shield_height : 0;
        if (old > 0 && old_height > keep) {
            keep = old_height;
        }
        shield_old_stop(keep);
    }
    if (old > 0) {
        shield_old_pid = old;
        shield_old_height = old_height;
        shield_old_due = now_ms() + KHOME_SHIELD_OVERLAP_MS;
    }
}

static void set_sheet_notice(const char *text)
{
    snprintf(sheet.notice, sizeof(sheet.notice), "%s", text);
}

static void draw_sheet(void)
{
    int h;
    const char *url = tabs_active_url(&tabs);

    if (!fb_matches_canvas()) {
        log_msg("sheet: framebuffer is %dx%d, not drawing", fb.width,
                fb.height);
        return;
    }
    sheet.tabs = tabs;
    sheet.hour = model.hour;
    sheet.minute = model.minute;
    sheet.battery_level = model.battery_level;
    sheet.favorite = url != NULL && favorite_saved(url);
    canvas_clear(&canvas, 255);
    draw_tab_sheet(&canvas, &fonts, &icons, &sheet, KHOME_SCALE);
    sheet.notice[0] = 0;
    h = sheet_height_px();
    if (fb_blit_rect(&fb, &canvas, 0, 0, fb.width, h) < 0) {
        log_msg("sheet: fb_blit_rect failed");
        return;
    }
    if (fb_refresh_rect(&fb, 0, 0, fb.width, h) < 0) {
        log_msg("sheet: refresh failed");
    }
    /* Closing a background tab shrinks the sheet.  The shield has to follow,
     * or the stale rows it still covers never get the browser's repaint. */
    shield_sync();
}

/* Repaints only the sheet's status strip for a minute tick or a battery
 * change, so the tab rows and buttons are not redrawn. */
static void draw_sheet_status(void)
{
    int px;
    int py;
    int pw;
    int ph;

    if (!fb_matches_canvas()) {
        log_msg("sheet: status draw skipped at %dx%d", fb.width, fb.height);
        return;
    }
    sheet.hour = model.hour;
    sheet.minute = model.minute;
    sheet.battery_level = model.battery_level;
    draw_tab_sheet_status(&canvas, &fonts, &icons, &sheet, KHOME_SCALE);
    scale_layout_rect(0, 0, 1072, 48, &px, &py, &pw, &ph);
    if (fb_blit_rect(&fb, &canvas, px, py, pw, ph) < 0) {
        log_msg("sheet: status fb_blit_rect failed");
        return;
    }
    if (fb_refresh_rect(&fb, px, py, pw, ph) < 0) {
        log_msg("sheet: status refresh failed");
    }
}

/* Puts the browser back under the sheet's rectangle.  X's own expose handler
 * repaints into /dev/fb0, then fbink pushes that rectangle to the panel. */
static void hide_sheet(void)
{
    char x[16];
    char y[16];
    char w[16];
    char h[16];
    char out[256];
    char *argv[] = { KHOME_XEXPOSE, x, y, w, h, NULL };
    int height = sheet_height_px();

    /* A tab closed with the state change leaves the sheet shorter than the
     * shield that was just stopped, so expose the old shield's rectangle. */
    if (height < shield_last_height) {
        height = shield_last_height;
    }
    shield_last_height = 0;
    snprintf(x, sizeof(x), "%d", 0);
    snprintf(y, sizeof(y), "%d", 0);
    snprintf(w, sizeof(w), "%d", fb.width);
    snprintf(h, sizeof(h), "%d", height);
    if (run_capture(argv, out, sizeof(out)) < 0) {
        log_msg("sheet: xexpose failed, the browser image stays stale");
    } else {
        log_msg("sheet: xexpose %s", out);
    }
    if (fb_refresh_rect(&fb, 0, 0, fb.width, height) < 0) {
        log_msg("sheet: refresh after hide failed");
    }
}

static void edit_refresh_rect(int x, int y, int w, int h)
{
    int px;
    int py;
    int pw;
    int ph;

    scale_layout_rect(x, y, w, h, &px, &py, &pw, &ph);
    if (fb_blit_rect(&fb, &canvas, px, py, pw, ph) < 0) {
        log_msg("edit: fb_blit_rect failed");
        return;
    }
    if (fb_refresh_rect(&fb, px, py, pw, ph) < 0) {
        log_msg("edit: refresh failed");
    }
}

static void draw_edit_field(void)
{
    int x;
    int y;
    int w;
    int h;

    layout_url_edit_field_rect(&x, &y, &w, &h);
    draw_url_edit_field(&canvas, &fonts, &edit, KHOME_SCALE);
    edit_refresh_rect(x, y, w, h);
}

static void draw_edit_keyboard(void)
{
    int x;
    int y;
    int w;
    int h;

    layout_url_edit_keyboard_rect(&x, &y, &w, &h);
    draw_url_edit_keyboard(&canvas, &fonts, &edit, KHOME_SCALE);
    edit_refresh_rect(x, y, w, h);
}

static void draw_edit_full(void)
{
    canvas_clear(&canvas, 255);
    draw_url_edit(&canvas, &fonts, &edit, KHOME_SCALE);
    if (fb_blit(&fb, &canvas) < 0) {
        log_msg("edit: fb_blit failed");
        return;
    }
    if (fb_refresh_full(&fb) < 0) {
        log_msg("edit: full refresh failed");
    }
}

static void edit_begin(void)
{
    const char *url = tabs_active_url(&tabs);

    if (edit_mode == KHOME_EDIT_NEW) {
        urledit_init(&edit, "");
    } else {
        urledit_init(&edit, url != NULL ? url : "");
    }
    draw_edit_full();
}

/* The editor covers everything, so the browser needs its whole rectangle
 * exposed before the sheet can come back over the top of it. */
static void hide_edit(void)
{
    char x[16];
    char y[16];
    char w[16];
    char h[16];
    char out[256];
    char *argv[] = { KHOME_XEXPOSE, x, y, w, h, NULL };

    snprintf(x, sizeof(x), "%d", 0);
    snprintf(y, sizeof(y), "%d", 0);
    snprintf(w, sizeof(w), "%d", fb.width);
    snprintf(h, sizeof(h), "%d", fb.height);
    if (run_capture(argv, out, sizeof(out)) < 0) {
        log_msg("edit: xexpose failed, the browser image stays stale");
    } else {
        log_msg("edit: xexpose %s", out);
    }
    if (fb_refresh_rect(&fb, 0, 0, fb.width, fb.height) < 0) {
        log_msg("edit: refresh after hide failed");
    }
}

static void edit_commit(void)
{
    if (edit_mode == KHOME_EDIT_NEW) {
        if (tabs_open(&tabs, edit_open_url) < 0) {
            log_msg("edit: cannot open a new tab: %s", edit_open_url);
            return;
        }
    } else if (tabs_set_active_url(&tabs, edit_open_url) < 0 &&
               tabs_open(&tabs, edit_open_url) < 0) {
        log_msg("edit: cannot open the URL: %s", edit_open_url);
        return;
    }
    write_tabs();
}

/* The top-edge strip that keeps a sheet-pull swipe away from the browser.
 * khome still reads event1 without grabbing it; the strip only stops X from
 * delivering that touch to the browser window. */
static void band_start(void)
{
    char w[16];
    char h[16];
    pid_t pid;

    if (band_pid > 0) {
        return;
    }
    snprintf(w, sizeof(w), "%d", fb.width);
    snprintf(h, sizeof(h), "%d", GESTURE_SHEET_EDGE_PX + 1);
    pid = fork();
    if (pid < 0) {
        log_msg("band: fork failed: %s", strerror(errno));
        return;
    }
    if (pid == 0) {
        /* fd 9 is the wrapper's flock on khome.pid; an orphaned helper must
         * not keep the next wrapper from starting. */
        close(9);
        execl(KHOME_XBAND, "xband", "0", "0", w, h, (char *)NULL);
        _exit(127);
    }
    band_pid = pid;
    log_msg("band: started pid %d over 0,0 %sx%s", (int)pid, w, h);
}

static void helper_stop(pid_t *pid, const char *name)
{
    int status;
    int i;

    if (*pid <= 0) {
        return;
    }
    kill(*pid, SIGTERM);
    for (i = 0; i < 20; i++) {
        if (waitpid(*pid, &status, WNOHANG) == *pid) {
            *pid = 0;
            break;
        }
        sleep_ms(10);
    }
    if (*pid > 0) {
        kill(*pid, SIGKILL);
        waitpid(*pid, &status, 0);
        *pid = 0;
    }
    log_msg("%s: stopped", name);
}

static void band_stop(void)
{
    helper_stop(&band_pid, "band");
}

/* The browser owns the rectangle under the sheet and repaints it on a page
 * load.  khome draws the sheet straight into /dev/fb0, so X has no damage
 * there and the browser's pixels land on top.  A background-less window over
 * the sheet's rectangle takes that area out of the browser's clip; the server
 * paints nothing into it, so the sheet stays. */
static void shield_start(int height)
{
    char w[16];
    char h[16];
    pid_t pid;

    if (shield_pid > 0) {
        return;
    }
    snprintf(w, sizeof(w), "%d", fb.width);
    snprintf(h, sizeof(h), "%d", height);
    pid = fork();
    if (pid < 0) {
        log_msg("shield: fork failed: %s", strerror(errno));
        return;
    }
    if (pid == 0) {
        /* fd 9 is the wrapper's flock on khome.pid; an orphaned helper must
         * not keep the next wrapper from starting. */
        close(9);
        execl(KHOME_XBAND, "xband", "0", "0", w, h, "shield", (char *)NULL);
        _exit(127);
    }
    shield_pid = pid;
    shield_height = height;
    log_msg("shield: started pid %d over 0,0 %sx%s", (int)pid, w, h);
}

/* Repaints the band an old shield leaves behind, using the same xexpose then
 * fbink push as hide_sheet() and hide_edit(). */
static void shield_expose_band(int top, int bottom)
{
    char x[16];
    char y[16];
    char w[16];
    char h[16];
    char out[256];
    char *argv[] = { KHOME_XEXPOSE, x, y, w, h, NULL };
    int height = bottom - top;

    if (height <= 0) {
        return;
    }
    snprintf(x, sizeof(x), "%d", 0);
    snprintf(y, sizeof(y), "%d", top);
    snprintf(w, sizeof(w), "%d", fb.width);
    snprintf(h, sizeof(h), "%d", height);
    if (run_capture(argv, out, sizeof(out)) < 0) {
        log_msg("shield: xexpose failed, the browser image stays stale");
    } else {
        log_msg("shield: xexpose %s", out);
    }
    if (fb_refresh_rect(&fb, 0, top, fb.width, height) < 0) {
        log_msg("shield: refresh after stop failed");
    }
}

static void shield_old_clear(void)
{
    helper_stop(&shield_old_pid, "shield");
    shield_old_height = 0;
    shield_old_due = 0;
}

/* Stops the deferred shield and repaints whatever it covered above keep. */
static void shield_old_stop(int keep)
{
    int height = shield_old_height;

    if (shield_old_pid <= 0) {
        return;
    }
    shield_old_clear();
    if (height > keep) {
        shield_expose_band(keep, height);
    }
}

static void shield_old_tick(void)
{
    int keep;

    if (shield_old_pid <= 0 || now_ms() < shield_old_due) {
        return;
    }
    keep = shield_pid > 0 ? shield_height : 0;
    shield_old_stop(keep);
}

static void shield_stop(void)
{
    int last = shield_pid > 0 ? shield_height : 0;

    if (shield_old_pid > 0 && shield_old_height > last) {
        last = shield_old_height;
    }
    /* The hide or full redraw that follows a stop covers this height, so
     * remember it for hide_sheet() when a tab closed with the state change. */
    shield_old_clear();
    helper_stop(&shield_pid, "shield");
    shield_last_height = last;
    shield_height = 0;
}

/* The strip is only needed while the browser (or the sheet over it) is
 * visible.  Keeping it across BROWSER <-> SHEET avoids a gap when the sheet
 * closes and the browser is in front again. */
static int band_state(KhomeState s)
{
    /* The band is an X window in the panel's portrait coordinates.  In
     * landscape the X screen is 1024x758 and khome still reads the touch
     * panel's portrait coordinates, so a 758x97 strip at 0,0 would both miss
     * the browser's top edge and steal part of the page.  Leave the band off
     * rather than guess a mapping that has not been measured on the device. */
    if (model.browser_landscape) {
        return 0;
    }
    return s == KHOME_STATE_BROWSER || s == KHOME_STATE_SHEET ||
           s == KHOME_STATE_EDIT;
}

static void sync_band(KhomeState prev, KhomeState next)
{
    int had = band_state(prev);
    int want = band_state(next);

    if (want && !had) {
        band_start();
    } else if (!want && had) {
        band_stop();
    }
}

/* True only while khome is actually painting around a landscape browser.
 * The browser setting stays landscape on HOME, NEWS and SETTINGS too, but
 * those screens are portrait and must keep the portrait pull mapping. */
static int screen_landscape(void)
{
    return model.browser_landscape &&
           (state == KHOME_STATE_BROWSER || state == KHOME_STATE_SHEET ||
            state == KHOME_STATE_EDIT);
}

static void start_browser(void)
{
    const char *url;

    read_tabs(&tabs);
    browser_orientation_refresh();
    url = tabs_active_url(&tabs);
    if (url == NULL) {
        url = KHOME_DEFAULT_URL;
        if (tabs_open(&tabs, url) >= 0) {
            write_tabs();
        }
    }
    open_url(url);
}

/* A favorite opens as a tab: reuse the exact one, else the tab on the same
 * host, instead of piling up duplicates as the browser navigates away. */
static int favorite_host_tab(const TabList *tabs, const char *url)
{
    char want[KHOME_TAB_HOST_MAX];
    char host[KHOME_TAB_HOST_MAX];
    char path[KHOME_TAB_PATH_MAX];
    int i;

    tabs_url_parts(url, want, sizeof(want), path, sizeof(path));
    if (want[0] == 0) {
        return -1;
    }
    /* The active tab first, then the newest: tabs are stored in open order. */
    if (tabs->active >= 0 && tabs->active < tabs->count) {
        tabs_url_parts(tabs->tabs[tabs->active].url, host, sizeof(host), path,
                       sizeof(path));
        if (strcmp(host, want) == 0) {
            return tabs->active;
        }
    }
    for (i = tabs->count - 1; i >= 0; i--) {
        if (i == tabs->active) {
            continue;
        }
        tabs_url_parts(tabs->tabs[i].url, host, sizeof(host), path,
                       sizeof(path));
        if (strcmp(host, want) == 0) {
            return i;
        }
    }
    return -1;
}

static void open_favorite(void)
{
    const char *url;
    int i;

    if (favorite_index < 0 || favorite_index >= model.favorite_count) {
        log_msg("favorites: no entry %d", favorite_index);
        return;
    }
    url = model.favorites[favorite_index].url;
    read_tabs(&tabs);
    for (i = 0; i < tabs.count; i++) {
        if (strcmp(tabs.tabs[i].url, url) == 0) {
            tabs_select(&tabs, i);
            write_tabs();
            open_url(url);
            return;
        }
    }
    i = favorite_host_tab(&tabs, url);
    if (i >= 0 && tabs_set_url(&tabs, i, url) == 0) {
        tabs_select(&tabs, i);
        write_tabs();
        open_url(url);
        return;
    }
    if (tabs_open(&tabs, url) < 0) {
        log_msg("favorites: cannot open a tab for %s", url);
        return;
    }
    write_tabs();
    open_url(url);
}

static int browser_event_args(const LipcEvent *ev)
{
    int i;

    for (i = 0; i < ev->arg_count; i++) {
        if (strstr(ev->args[i], KHOME_APP_BROWSER) != NULL ||
            strstr(ev->args[i], KHOME_BROWSER_COMM) != NULL) {
            return 1;
        }
    }
    return 0;
}

/* appmgrd sends `processExited "com.lab126.browser" <pid> <status>`, where
 * status 0 is a clean stop and 1 is a kill. */
static int browser_process_exit(const LipcEvent *ev, int *abnormal)
{
    char *end = NULL;
    long status;

    if (strcmp(ev->name, "processExited") != 0 || ev->arg_count < 3) {
        return 0;
    }
    if (!browser_event_args(ev)) {
        return 0;
    }
    status = strtol(ev->args[2], &end, 10);
    if (end == ev->args[2]) {
        /* The browser died but the status was not a number; close the alert
         * the way an abnormal exit does. */
        *abnormal = 1;
        return 1;
    }
    *abnormal = status != 0;
    return 1;
}

/* Drain the appmgrd watch while browser_stop() waits.  Nothing else runs the
 * main loop then, so events read here would otherwise stay queued.  The state
 * is already HOME on the close path, and on_app_event ignores processExited
 * outside BROWSER/SHEET/EDIT, so dropping them is safe. */
static int app_watch_browser_exit(int *abnormal)
{
    LipcEvent ev;
    int got;
    int seen = 0;

    if (app_watch.fd < 0) {
        return 0;
    }
    while ((got = lipc_watch_read(&app_watch, &ev)) == 1) {
        int bad;

        if (browser_process_exit(&ev, &bad)) {
            seen = 1;
            *abnormal = bad;
        }
    }
    if (got < 0) {
        lipc_watch_stop(&app_watch);
    }
    return seen;
}

/* appmgrd raises this alert whenever the browser dies; it floats above khome
 * until its close button is tapped. */
static void dismiss_alert(void)
{
    int64_t deadline;
    int64_t settle;

    if (asleep) {
        alert_pending = 1;
        log_msg("browser: alert dismissal deferred until wake");
        return;
    }
    deadline = now_ms() + KHOME_ALERT_TIMEOUT_MS;
    while (now_ms() < deadline) {
        if (window_viewable("pillowAlert")) {
            break;
        }
        sleep_ms(KHOME_BROWSER_POLL_MS);
    }
    if (window_viewable("pillowAlert")) {
        log_msg("browser: alert shown, tapping close");
    } else {
        log_msg("browser: no alert appeared, tapping anyway");
    }
    grab_touch(0);
    /* pillow ignores taps for about 4.5 s after it maps the alert, then
     * closes it within 0.5 s of a tap.  Tap until it goes, giving each tap
     * time to close it so no tap lands on what is underneath. */
    deadline = now_ms() + KHOME_ALERT_DISMISS_MS;
    do {
        input_drain(&touch, &gesture);
        inject_tap(KHOME_ALERT_X, KHOME_ALERT_Y);
        settle = now_ms() + 8 * KHOME_BROWSER_POLL_MS;
        while (now_ms() < settle && window_viewable("pillowAlert")) {
            sleep_ms(KHOME_BROWSER_POLL_MS);
        }
    } while (window_viewable("pillowAlert") && now_ms() < deadline);
    input_drain(&touch, &gesture);
    grab_touch(1);
}

/* The browser's device.orientation rotates the panel; when its window goes,
 * winmgr should put it back to 758x1024.  Re-read the geometry instead of
 * trusting fb_open()'s values, and give winmgr a moment to finish.  Drawing
 * home against a landscape framebuffer would copy rows with the wrong stride
 * (or fail outright), so every path that returns from the browser waits here
 * before the redraw. */
static void wait_for_portrait(void)
{
    int64_t deadline = now_ms() + KHOME_PORTRAIT_WAIT_MS;

    for (;;) {
        if (fb_refresh_geometry(&fb) == 0 && fb.width == canvas.w &&
            fb.height == canvas.h) {
            return;
        }
        if (now_ms() >= deadline) {
            break;
        }
        sleep_ms(50);
    }
    log_msg("fb: still %dx%d after %d ms, not portrait", fb.width, fb.height,
            KHOME_PORTRAIT_WAIT_MS);
}

/* appmgrd's stop path lets the browser unload through the framework, so
 * appmgrd does not treat it as a failed app and raise its alert.  It only
 * works when appmgrd can hand GO to com.lab126.booklet.home; fakekaf
 * answers as that home booklet.  kill 1 stays the fallback for a browser that
 * ignores the request, and its alert still has to be tapped away.
 * The exit path uses this too, so it leaves the repaint to browser_close(). */
static void browser_stop(void)
{
    int clean = 0;
    int exit_seen = 0;
    int abnormal = 0;
    int first = 1;
    int64_t started;
    int64_t deadline;

    /* An exit queued before the stop is this browser's crash when the process
     * is already gone; with a browser still running it is left from an earlier
     * one and must not be read as this stop's exit. */
    if (app_watch_browser_exit(&abnormal) &&
        !browser_running()) {
        exit_seen = 1;
    } else {
        abnormal = 0;
    }
    log_msg("browser: appmgrd stop");
    if (lipc_set_str("com.lab126.appmgrd", "stop", KHOME_APP_BROWSER) < 0) {
        log_msg("browser: stop failed");
    }
    started = now_ms();
    deadline = started + KHOME_KILL_TIMEOUT_MS;
    for (;;) {
        if (app_watch_browser_exit(&abnormal)) {
            exit_seen = 1;
        }
        if (!browser_running()) {
            clean = 1;
            break;
        }
        first = 0;
        if (now_ms() >= deadline) {
            break;
        }
        sleep_ms(KHOME_BROWSER_POLL_MS);
    }
    if (clean) {
        log_msg("browser: stopped after about %d ms",
                (int)(now_ms() - started));
        /* appmgrd reports the exit after pause/unload; it can land just
         * after the process disappears.  The event says whether this was a
         * clean stop or a kill, so wait for it instead of guessing from the
         * first-poll timing. */
        if (!exit_seen) {
            deadline = now_ms() + KHOME_ALERT_TIMEOUT_MS;
            /* With the watcher down no event can come. */
            while (app_watch.fd >= 0 && now_ms() < deadline) {
                if (app_watch_browser_exit(&abnormal)) {
                    exit_seen = 1;
                    break;
                }
                sleep_ms(KHOME_BROWSER_POLL_MS);
            }
        }
        if (exit_seen) {
            log_msg("browser: processExited abnormal=%d", abnormal);
        }
        /* A clean stop leaves no alert, and dismiss_alert() would block for
         * up to ~10 s waiting for one.  An alert left from an earlier crash
         * still needs the tap, and an abnormal exit maps one a moment after
         * its event.  Without an event, fall back to the old first-poll guess:
         * a browser already gone then crashed before the stop reached it. */
        if (!exit_seen && first) {
            deadline = now_ms() + KHOME_ALERT_TIMEOUT_MS;
            while (now_ms() < deadline) {
                if (window_viewable("pillowAlert")) {
                    break;
                }
                sleep_ms(KHOME_BROWSER_POLL_MS);
            }
        }
        if (window_viewable("pillowAlert")) {
            log_msg("browser: alert was already up, dismissing");
            dismiss_alert();
        } else if (exit_seen && abnormal) {
            dismiss_alert();
        }
    } else {
        log_msg("browser: still running after %d ms, falling back to kill 1",
                (int)(now_ms() - started));
        /* kill is a string property; `lipc-set-prop -i ... kill 1` returns
         * lipcErrNoSuchProperty. */
        if (lipc_set_str(KHOME_APP_BROWSER, "kill", "1") < 0) {
            log_msg("browser: kill failed");
        }
        started = now_ms();
        deadline = started + KHOME_KILL_TIMEOUT_MS;
        for (;;) {
            if (!browser_running()) {
                break;
            }
            if (now_ms() >= deadline) {
                break;
            }
            sleep_ms(KHOME_BROWSER_POLL_MS);
        }
        if (browser_running()) {
            log_msg("browser: still running after %d ms",
                    (int)(now_ms() - started));
        } else {
            log_msg("browser: exited after about %d ms",
                    (int)(now_ms() - started));
        }
        dismiss_alert();
    }
    wait_for_portrait();
}

static void browser_close(void)
{
    browser_stop();
    if (!asleep) {
        model_update(&model);
        draw_frame(1);
    }
}

/* The browser's own settings are the only orientation record khome keeps.
 * Stop it first (WebKit holds localStorage in memory), change only the
 * orientation key, then read the row back before trusting it.  The browser is
 * not relaunched: the next Browser tap opens it in the new orientation. */
static void browser_orientation_toggle(void)
{
    char before_json[KHOME_BROWSER_SETTINGS_MAX];
    char before_hex[KHOME_BROWSER_SETTINGS_HEX_MAX];
    int before_valid = 0;
    int want = !model.browser_landscape;

    if (browser_running()) {
        log_msg("browser: stopping before the orientation change");
        browser_stop();
    }
    wait_for_portrait();
    if (browser_settings_read(before_json, sizeof(before_json), before_hex,
                              sizeof(before_hex), &before_valid) < 0 ||
        !before_valid) {
        log_msg("browser: orientation not changed: settings unreadable");
        return;
    }
    if (browser_settings_write_orientation(want) < 0) {
        log_msg("browser: orientation write failed, restoring settings");
        if (browser_settings_restore(before_hex) < 0) {
            log_msg("browser: settings restore failed");
        }
        return;
    }
    {
        char json[KHOME_BROWSER_SETTINGS_MAX];
        int valid = 0;
        int landscape;

        if (browser_settings_read(json, sizeof(json), NULL, 0, &valid) < 0 ||
            !valid) {
            landscape = -1;
        } else {
            landscape = tabs_browser_orientation(json) ==
                        BROWSER_ORIENTATION_LANDSCAPE;
        }
        if (landscape != want) {
            log_msg("browser: orientation verify failed, restoring settings");
            if (browser_settings_restore(before_hex) < 0) {
                log_msg("browser: settings restore failed");
            } else {
                log_msg("browser: settings restored");
            }
            return;
        }
    }
    model.browser_landscape = want;
    log_msg("browser: orientation set to %s",
            want ? "landscapeLeft" : "auto");
    draw_frame(1);
}

static void start_koreader(void)
{
    pid_t pid = fork();

    if (pid < 0) {
        log_msg("koreader: fork failed: %s", strerror(errno));
        return;
    }
    if (pid == 0) {
        /* One group for koreader.sh and reader.lua so an exit can take both
         * down without guessing pids. */
        setpgid(0, 0);
        setenv("PATH", KHOME_TOOL_PATH, 1);
        setenv("DISPLAY", ":0", 1);
        setenv("TZ", "JST-9", 1);
        if (chdir("/mnt/us/koreader") != 0) {
            _exit(126);
        }
        execl("/bin/sh", "sh", KHOME_KOREADER, (char *)NULL);
        _exit(127);
    }
    /* Either side may win the race; setting it here means the group is there
     * before cleanup can reach for it. */
    setpgid(pid, pid);
    koreader_pid = pid;
    log_msg("koreader: started pid %d", (int)pid);
}

/* KOReader does not always reach its own cleanup, so khome kills the group and
 * makes sure volumd is running before it exits. */
static void koreader_stop(void)
{
    int status;
    int i;
    int leader_gone = 0;
    pid_t pgid;

    if (koreader_pid <= 0) {
        return;
    }
    pgid = koreader_pid;
    log_msg("koreader: terminating group %d", (int)pgid);
    /* The TERM also kills koreader.sh before its own cleanup resumes awesome,
     * and the wrapper's restore only restarts x for a KOReader it still sees.
     * The marker tells it this one was killed here, and it has to exist before
     * the kill: the wrapper may SIGKILL khome during the wait below. */
    close(open(KHOME_KOREADER_KILLED, O_WRONLY | O_CREAT | O_TRUNC, 0644));
    kill(-pgid, SIGTERM);
    for (i = 0; i < 2000 / 10; i++) {
        pid_t got = waitpid(pgid, &status, WNOHANG);
        if (got == pgid || (got < 0 && errno == ECHILD)) {
            leader_gone = 1;
            break;
        }
        if (got < 0 && errno != EINTR) {
            leader_gone = 1;
            break;
        }
        sleep_ms(10);
    }
    /* The group leader can exit first while reader.lua and the rest stay in
     * the group, so a reaped leader does not mean the group is gone. */
    if (kill(-pgid, 0) == 0) {
        kill(-pgid, SIGKILL);
    }
    if (!leader_gone) {
        while (waitpid(pgid, &status, 0) < 0) {
            if (errno != EINTR) {
                break;
            }
        }
    }
    koreader_pid = 0;
    resume_volumd();
}

static void sheet_reload(void)
{
    const char *url;

    browser_capture_url();
    url = tabs_active_url(&tabs);
    if (url == NULL) {
        log_msg("sheet: no tab to reload");
        return;
    }
    open_url(url);
}

static void sheet_tab(void)
{
    const char *url;

    browser_capture_url();
    if (tabs_select(&tabs, sheet_index) < 0) {
        log_msg("sheet: no tab %d", sheet_index);
        return;
    }
    write_tabs();
    url = tabs_active_url(&tabs);
    if (url != NULL) {
        open_url(url);
    }
}

static void sheet_close(void)
{
    const char *url;
    int was_active;

    browser_capture_url();
    was_active = sheet_index == tabs.active;
    if (tabs_close(&tabs, sheet_index) < 0 && tabs.count > 0) {
        log_msg("sheet: no tab %d", sheet_index);
        return;
    }
    write_tabs();
    if (was_active) {
        url = tabs_active_url(&tabs);
        if (url != NULL) {
            open_url(url);
        }
    }
    draw_sheet();
}

static void sheet_close_active(void)
{
    const char *url;

    browser_capture_url();
    if (tabs_close(&tabs, sheet_index) < 0) {
        log_msg("sheet: no tab %d", sheet_index);
        return;
    }
    write_tabs();
    url = tabs_active_url(&tabs);
    if (url != NULL) {
        open_url(url);
    }
}

static void sheet_close_last(void)
{
    if (tabs_close(&tabs, sheet_index) == -1) {
        log_msg("sheet: closed the last tab, count %d", tabs.count);
    }
    write_tabs();
    browser_close();
}

static void sheet_favorite_toggle(void)
{
    const char *url;
    const char *title;
    int index;

    browser_capture_url();
    url = tabs_active_url(&tabs);
    if (url == NULL) {
        log_msg("favorites: no open tab");
        return;
    }
    index = favorites_find(model.favorites, model.favorite_count, url);
    if (index >= 0) {
        remove_favorite_at(index);
        draw_sheet();
        return;
    } else {
        if (model.favorite_count >= KHOME_MAX_FAVORITES) {
            log_msg("favorites: full (%d), not adding %s", model.favorite_count,
                    url);
            set_sheet_notice("Favorites are full (64). Remove one first.");
            draw_sheet();
            return;
        }
        title = strcmp(page_title_url, url) == 0 ? page_title : "";
        if (favorites_add(model.favorites, &model.favorite_count,
                          KHOME_MAX_FAVORITES, url, title) < 0) {
            log_msg("favorites: cannot add %s", url);
            set_sheet_notice("Could not add this favorite.");
            draw_sheet();
            return;
        }
        log_msg("favorites: added %s", url);
    }
    write_favorites();
    draw_sheet();
}

static void edit_submit(void)
{
    char url[KHOME_TAB_URL_MAX];
    int len = urledit_normalize(&edit, url, sizeof(url));

    if (len == 0) {
        log_msg("edit: empty URL, returning to the sheet");
        set_sheet_notice("Enter a URL first.");
        handle_event(KHOME_EV_EDIT_CANCEL);
        return;
    }
    if (len < 0) {
        log_msg("edit: URL too long, staying");
        return;
    }
    memcpy(edit_open_url, url, (size_t)len + 1);
    handle_event(KHOME_EV_EDIT_OPEN);
}

static void edit_insert(const char *text)
{
    int shifted = edit.shift;

    if (!urledit_insert(&edit, text)) {
        log_msg("edit: URL is full");
        return;
    }
    edit.shift = 0;
    draw_edit_field();
    if (shifted) {
        draw_edit_keyboard();
    }
}

static void edit_key(const char *key)
{
    if (strcmp(key, "shift") == 0) {
        edit.shift = !edit.shift;
        draw_edit_keyboard();
    } else if (strcmp(key, "sym") == 0) {
        edit.symbols = !edit.symbols;
        edit.shift = 0;
        draw_edit_keyboard();
    } else if (strcmp(key, "bs") == 0) {
        if (urledit_backspace(&edit)) {
            draw_edit_field();
        }
    } else if (strcmp(key, "left") == 0) {
        urledit_move(&edit, -1);
        draw_edit_field();
    } else if (strcmp(key, "right") == 0) {
        urledit_move(&edit, 1);
        draw_edit_field();
    } else if (strcmp(key, ".com") == 0) {
        edit_insert(".com");
    } else if (key[0] != 0 && key[1] == 0) {
        char text[2];
        text[0] = key[0];
        text[1] = 0;
        if (edit.shift && text[0] >= 'a' && text[0] <= 'z') {
            text[0] = (char)(text[0] - 'a' + 'A');
        }
        edit_insert(text);
    } else {
        log_msg("edit: unknown key %s", key);
    }
}

static void schedule(KhomeEvent ev, int delay_ms)
{
    defer_event = ev;
    defer_due = now_ms() + delay_ms;
    defer_active = 1;
}

static int home_page_count(void)
{
    return model.favorite_count > 6 ? (model.favorite_count + 5) / 6 : 1;
}

static void home_page_step(int delta)
{
    int next = model.page + delta;

    if (next < 0 || next >= home_page_count()) {
        return;
    }
    model.page = next;
    draw_frame(1);
}

/* The app switch is slow enough to see, so show which app is starting before
 * handing the screen over. */
static void draw_launching_screen(const char *name, int icon)
{
    LaunchingModel launching;

    memset(&launching, 0, sizeof(launching));
    launching.home = model;
    snprintf(launching.app_name, sizeof(launching.app_name), "%s", name);
    launching.app_icon = icon;
    canvas_clear(&canvas, 255);
    draw_launching(&canvas, &fonts, &icons, &launching, KHOME_SCALE);
    if (fb_blit(&fb, &canvas) < 0) {
        log_msg("launching: fb_blit failed");
        return;
    }
    if (fb_refresh_full(&fb) < 0) {
        log_msg("launching: refresh failed");
    }
}

static void run_action(KhomeAction act)
{
    switch (act) {
    case KHOME_ACT_GRAB:
        grab_touch(1);
        break;
    case KHOME_ACT_UNGRAB:
        grab_touch(0);
        break;
    case KHOME_ACT_DRAW_HOME:
    case KHOME_ACT_DRAW_SETTINGS:
    case KHOME_ACT_DRAW_CONFIRM:
        model_update(&model);
        draw_frame(1);
        break;
    case KHOME_ACT_DRAW_NEWS:
        model_update(&model);
        if (news_edition_update() || !news_have_paper) {
            news_reload();
        }
        news_draw();
        break;
    case KHOME_ACT_DRAW_NEWS_STATUS:
        news_status_draw();
        break;
    case KHOME_ACT_DRAW_NEWS_MENU:
        model_update(&model);
        news_menu_notice[0] = 0;
        if (news_edition_update() || !news_have_paper) {
            news_reload();
        }
        news_menu_draw();
        break;
    case KHOME_ACT_DRAW_NEWS_MENU_STATUS:
        news_menu_status_draw();
        break;
    case KHOME_ACT_NEWS_MENU_REFRESH:
        if (news_fetch_start(NEWS_FETCH_FROM_MENU)) {
            handle_event(KHOME_EV_SHEET_DISMISS);
        } else {
            news_menu_draw();
        }
        break;
    case KHOME_ACT_NEWS_PREV:
        news_page_step(-1);
        break;
    case KHOME_ACT_NEWS_NEXT:
        news_page_step(1);
        break;
    case KHOME_ACT_NEWS_FRONT:
        news_show_page(0);
        break;
    case KHOME_ACT_NEWS_BACK:
        news_back_to_paper();
        break;
    case KHOME_ACT_NEWS_FETCH:
        news_fetch_start(state == KHOME_STATE_SETTINGS ? NEWS_FETCH_FROM_SETTINGS
                                                       : NEWS_FETCH_FROM_NEWS);
        break;
    case KHOME_ACT_WIFI_TOGGLE:
        settings_wifi_toggle();
        break;
    case KHOME_ACT_CHARGE_LIMIT_TOGGLE:
        settings_charge_limit_toggle();
        break;
    case KHOME_ACT_BRIGHTNESS_DOWN:
        settings_brightness_step(-1);
        break;
    case KHOME_ACT_BRIGHTNESS_UP:
        settings_brightness_step(1);
        break;
    case KHOME_ACT_DRAW_CLOCK:
        draw_frame(0);
        break;
    case KHOME_ACT_PAGE_PREV:
        home_page_step(-1);
        break;
    case KHOME_ACT_PAGE_NEXT:
        home_page_step(1);
        break;
    case KHOME_ACT_START_BROWSER:
        draw_launching_screen("Browser", ICON_BROWSER);
        start_browser();
        break;
    case KHOME_ACT_FAVORITE_OPEN:
        draw_launching_screen("Browser", ICON_BROWSER);
        open_favorite();
        break;
    case KHOME_ACT_CLOSE_BROWSER:
        browser_close();
        break;
    case KHOME_ACT_START_KOREADER:
        draw_launching_screen("Manga", ICON_MANGA);
        start_koreader();
        break;
    case KHOME_ACT_SCHEDULE_REDRAW:
        schedule(KHOME_EV_DEFER_REDRAW, KHOME_WAKE_DELAY_MS);
        break;
    case KHOME_ACT_DRAW_SHEET:
        read_tabs(&tabs);
        browser_capture_url();
        read_favorites(&model);
        draw_sheet();
        break;
    case KHOME_ACT_DRAW_SHEET_STATUS:
        draw_sheet_status();
        break;
    case KHOME_ACT_HIDE_SHEET:
        hide_sheet();
        break;
    case KHOME_ACT_DRAW_EDIT:
        edit_begin();
        break;
    case KHOME_ACT_HIDE_EDIT:
        hide_edit();
        break;
    case KHOME_ACT_EDIT_OPEN:
        edit_commit();
        break;
    case KHOME_ACT_OPEN_URL:
        open_url(edit_open_url);
        break;
    case KHOME_ACT_BROWSER_BACK:
        if (lipc_set_str(KHOME_APP_BROWSER, "searchBarButtonSelected",
                         "back") == 0) {
            log_msg("browser: back requested");
        } else {
            log_msg("browser: back failed");
        }
        break;
    case KHOME_ACT_SHEET_TAB:
        sheet_tab();
        break;
    case KHOME_ACT_SHEET_FAVORITE:
        sheet_favorite_toggle();
        break;
    case KHOME_ACT_SHEET_CLOSE:
        sheet_close();
        break;
    case KHOME_ACT_SHEET_CLOSE_ACTIVE:
        sheet_close_active();
        break;
    case KHOME_ACT_SHEET_CLOSE_LAST:
        sheet_close_last();
        break;
    case KHOME_ACT_SHEET_RELOAD:
        sheet_reload();
        break;
    case KHOME_ACT_QUIT_REBOOT_HOME:
        quit_code = KHOME_EXIT_REBOOT_HOME;
        quit = 1;
        break;
    case KHOME_ACT_QUIT_REBOOT_KINDLE:
        quit_code = KHOME_EXIT_REBOOT_KINDLE;
        quit = 1;
        break;
    case KHOME_ACT_QUIT:
        quit = 1;
        break;
    case KHOME_ACT_REMOVE_FAVORITE:
        remove_favorite_at(favorite_index);
        break;
    }
}

static void handle_event(KhomeEvent ev)
{
    KhomeMachine machine;
    KhomeStep step;
    KhomeState prev = state;
    int was_asleep = asleep;
    int i;

    machine.state = state;
    machine.asleep = asleep;
    machine.confirm = confirm;
    step = khome_step(machine, ev);

    if (step.state == KHOME_STATE_NEWS && prev != KHOME_STATE_NEWS &&
        prev != KHOME_STATE_NEWS_MENU) {
        news_page = 0;
        news_view = NEWS_VIEW_PAGE;
        kt_article_close(&news_article);
    }
    if (step.state == KHOME_STATE_SETTINGS && prev == KHOME_STATE_HOME) {
        settings.notice[0] = 0;
        settings.updating = fetch_pid > 0;
    }

    log_msg("%s + %s -> %s", khome_state_name(state), khome_event_name(ev),
            khome_state_name(step.state));
    if (state != KHOME_STATE_BROWSER && state != KHOME_STATE_SHEET &&
        state != KHOME_STATE_EDIT && step.state == KHOME_STATE_BROWSER) {
        browser_since = now_ms();
        browser_seen = 0;
        /* A new attempt clears the previous failure from the footer. */
        model.notice[0] = 0;
    }
    /* The remove-favorite action still uses the index the dialog was opened
     * with, so a re-read here could point it at another entry. */
    if (state != KHOME_STATE_HOME && state != KHOME_STATE_HOME_CONFIRM &&
        step.state == KHOME_STATE_HOME) {
        read_favorites(&model);
    }
    state = step.state;
    asleep = step.asleep;
    confirm = step.confirm;
    if (state == KHOME_STATE_HOME && prev != KHOME_STATE_HOME) {
        browser_orientation_refresh();
    }
    /* The launch failure line belongs to home.  Leaving home for any other
     * screen clears it; a new launch already clears it above. */
    if (state != KHOME_STATE_HOME && state != KHOME_STATE_BROWSER) {
        model.notice[0] = 0;
    }
    shield_sync();
    if (asleep && !was_asleep) {
        /* A press held across sleep would otherwise release as a tap. */
        input_drain(&touch, &gesture);
        gesture_cancel(&gesture);
    }
    for (i = 0; i < step.action_count; i++) {
        log_msg("  action %s", khome_action_name(step.actions[i]));
        run_action(step.actions[i]);
    }
    if (!asleep && alert_pending) {
        alert_pending = 0;
        dismiss_alert();
    }
    sync_band(prev, state);
}

/* A crash takes the browser down without khome asking, so the exit paths must
 * close appmgrd's alert as well as browser_close() does. */
static void browser_exited(void)
{
    if (state == KHOME_STATE_BROWSER || state == KHOME_STATE_SHEET ||
        state == KHOME_STATE_EDIT) {
        /* The shield covers the alert's close button, so it has to come down
         * before the injected tap or the tap lands on the shield. */
        shield_stop();
        dismiss_alert();
    }
    wait_for_portrait();
    handle_event(KHOME_EV_BROWSER_EXIT);
}

static int point_in_rect(int x, int y, const TapRect *r)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static void on_edit_tap(int dx, int dy)
{
    TapRect rects[64];
    int count = layout_url_edit_taps(&edit, rects, 64);
    int i;

    for (i = 0; i < count; i++) {
        if (!point_in_rect(dx, dy, &rects[i])) {
            continue;
        }
        log_msg("tap action %s", rects[i].action);
        if (strcmp(rects[i].action, "urledit_cancel") == 0) {
            handle_event(KHOME_EV_EDIT_CANCEL);
        } else if (strcmp(rects[i].action, "urledit_open") == 0) {
            edit_submit();
        } else if (strcmp(rects[i].action, "urledit_clear") == 0) {
            urledit_clear(&edit);
            draw_edit_field();
        } else if (strcmp(rects[i].action, "urledit_field") == 0) {
            urledit_set_cursor(&edit,
                               layout_url_edit_cursor_at(&fonts, &edit, dx, dy));
            draw_edit_field();
        } else if (strncmp(rects[i].action, "key:", 4) == 0) {
            edit_key(rects[i].action + 4);
        } else {
            log_msg("tap ignored (%s)", rects[i].action);
        }
        return;
    }
    log_msg("tap outside every edit control");
}

static void on_sheet_tap(int dx, int dy)
{
    TapRect rects[24];
    int count;
    int i;

    count = layout_tab_sheet_taps(&sheet, rects, 24);
    for (i = 0; i < count; i++) {
        if (!point_in_rect(dx, dy, &rects[i])) {
            continue;
        }
        log_msg("tap action %s", rects[i].action);
        if (strcmp(rects[i].action, "sheet_back") == 0) {
            handle_event(KHOME_EV_SHEET_BACK);
        } else if (strcmp(rects[i].action, "sheet_home") == 0) {
            handle_event(KHOME_EV_SHEET_HOME);
        } else if (strcmp(rects[i].action, "sheet_new") == 0) {
            if (tabs.count >= KHOME_MAX_TABS) {
                log_msg("sheet: tabs are full (%d), not opening the editor",
                        tabs.count);
                set_sheet_notice("Tabs are full (8). Close one first.");
                draw_sheet();
                return;
            }
            edit_mode = KHOME_EDIT_NEW;
            handle_event(KHOME_EV_SHEET_NEW);
        } else if (strcmp(rects[i].action, "sheet_tab") == 0) {
            sheet_index = rects[i].index;
            handle_event(KHOME_EV_SHEET_TAB);
        } else if (strcmp(rects[i].action, "sheet_close") == 0) {
            sheet_index = rects[i].index;
            if (tabs.count <= 1) {
                handle_event(KHOME_EV_SHEET_CLOSE_LAST);
            } else if (rects[i].index == tabs.active) {
                handle_event(KHOME_EV_SHEET_CLOSE_ACTIVE);
            } else {
                handle_event(KHOME_EV_SHEET_CLOSE);
            }
        } else if (strcmp(rects[i].action, "sheet_edit_url") == 0) {
            edit_mode = KHOME_EDIT_CURRENT;
            handle_event(KHOME_EV_SHEET_EDIT);
        } else if (strcmp(rects[i].action, "sheet_favorite") == 0) {
            handle_event(KHOME_EV_SHEET_FAVORITE);
        } else if (strcmp(rects[i].action, "sheet_reload") == 0) {
            handle_event(KHOME_EV_SHEET_RELOAD);
        } else if (strcmp(rects[i].action, "sheet_dismiss") == 0) {
            handle_event(KHOME_EV_SHEET_DISMISS);
        } else {
            log_msg("tap ignored (%s)", rects[i].action);
        }
        return;
    }
    /* Outside the sheet body: close it. */
    log_msg("tap outside the sheet body");
    handle_event(KHOME_EV_SHEET_DISMISS);
}

static void on_settings_tap(int dx, int dy)
{
    TapRect rects[8];
    int count = layout_settings_taps(rects, 8);
    int i;

    for (i = 0; i < count; i++) {
        if (!point_in_rect(dx, dy, &rects[i])) {
            continue;
        }
        log_msg("tap action %s", rects[i].action);
        if (strcmp(rects[i].action, "back_home") == 0) {
            handle_event(KHOME_EV_SETTINGS_BACK);
        } else if (strcmp(rects[i].action, "reboot") == 0) {
            handle_event(KHOME_EV_SETTINGS_REBOOT);
        } else if (strcmp(rects[i].action, "stock_ui") == 0) {
            handle_event(KHOME_EV_SETTINGS_STOCK);
        } else if (strcmp(rects[i].action, "wifi_toggle") == 0) {
            handle_event(KHOME_EV_SETTINGS_WIFI);
        } else if (strcmp(rects[i].action, "charge_limit_toggle") == 0) {
            handle_event(KHOME_EV_SETTINGS_CHARGE_LIMIT);
        } else if (strcmp(rects[i].action, "brightness_down") == 0) {
            handle_event(KHOME_EV_SETTINGS_BRIGHTNESS_DOWN);
        } else if (strcmp(rects[i].action, "brightness_up") == 0) {
            handle_event(KHOME_EV_SETTINGS_BRIGHTNESS_UP);
        } else if (strcmp(rects[i].action, "news_update") == 0) {
            handle_event(KHOME_EV_SETTINGS_UPDATE);
        } else {
            log_msg("settings: %s not implemented", rects[i].action);
        }
        return;
    }
    log_msg("tap outside every settings control");
}

static void on_confirm_tap(int dx, int dy)
{
    TapRect rects[4];
    int count = layout_confirm_taps(confirm, rects, 4);
    int i;

    for (i = 0; i < count; i++) {
        if (!point_in_rect(dx, dy, &rects[i])) {
            continue;
        }
        log_msg("tap action %s", rects[i].action);
        if (strcmp(rects[i].action, "confirm_reboot_kindle") == 0) {
            handle_event(KHOME_EV_CONFIRM_REBOOT_KINDLE);
        } else if (strcmp(rects[i].action, "confirm_reboot_home") == 0) {
            handle_event(KHOME_EV_CONFIRM_REBOOT_HOME);
        } else if (strcmp(rects[i].action, "confirm_stock_back") == 0) {
            handle_event(KHOME_EV_CONFIRM_STOCK_BACK);
        } else if (strcmp(rects[i].action, "confirm_favorite_remove") == 0) {
            handle_event(KHOME_EV_CONFIRM_REMOVE_FAVORITE);
        } else if (strcmp(rects[i].action, "confirm_cancel") == 0) {
            handle_event(KHOME_EV_CONFIRM_CANCEL);
        }
        return;
    }
    log_msg("tap outside the confirm dialog");
    handle_event(KHOME_EV_CONFIRM_CANCEL);
}

static void on_news_menu_tap(int dx, int dy)
{
    NewsMenuModel m;
    TapRect rects[8];
    int count;
    int i;

    news_menu_model(&m);
    count = layout_news_menu_taps(&m, rects, 8);
    for (i = 0; i < count; i++) {
        if (!point_in_rect(dx, dy, &rects[i])) {
            continue;
        }
        log_msg("tap action %s", rects[i].action);
        if (strcmp(rects[i].action, "news_menu_home") == 0) {
            handle_event(KHOME_EV_NEWS_MENU_HOME);
        } else if (strcmp(rects[i].action, "news_menu_front") == 0) {
            handle_event(KHOME_EV_NEWS_MENU_FRONT);
        } else if (strcmp(rects[i].action, "news_menu_refresh") == 0) {
            handle_event(KHOME_EV_NEWS_MENU_REFRESH);
        } else if (strcmp(rects[i].action, "news_menu_back") == 0) {
            handle_event(KHOME_EV_NEWS_MENU_BACK);
        } else if (strcmp(rects[i].action, "news_menu_dismiss") == 0) {
            handle_event(KHOME_EV_SHEET_DISMISS);
        }
        return;
    }
    /* Outside the panel: close it and repaint the page under it. */
    log_msg("tap outside the news menu");
    handle_event(KHOME_EV_SHEET_DISMISS);
}

static void on_news_tap(Tap tap, int dx, int dy)
{
    TapRect rects[4];
    int count;
    int i;

    if (news_view == NEWS_VIEW_ARTICLE) {
        if (kt_article_font_at(tap.x, tap.y)) {
            log_msg("tap action news_font_size");
            news_text_size_cycle();
            return;
        }
        if (kt_article_next_at(&news_article, tap.x, tap.y)) {
            int next = news_article.story + 1;
            if (next < news_paper.story_count) {
                log_msg("tap action next article");
                news_open_story(next);
                return;
            }
        }
    } else if (news_have_paper) {
        int story = kt_paper_item_at(&news_paper, news_page, tap.x, tap.y);
        if (story < 0) {
            story = kt_paper_index_at(&news_paper, news_page, tap.x, tap.y);
        }
        if (story >= 0) {
            log_msg("tap action article %s",
                    news_paper.stories[story].id);
            news_open_story(story);
            return;
        }
    }
    count = layout_news_taps(rects, 4);
    for (i = 0; i < count; i++) {
        if (!point_in_rect(dx, dy, &rects[i])) {
            continue;
        }
        log_msg("tap action %s", rects[i].action);
        if (strcmp(rects[i].action, "news_prev") == 0) {
            handle_event(KHOME_EV_NEWS_PREV);
        } else if (strcmp(rects[i].action, "news_next") == 0) {
            handle_event(KHOME_EV_NEWS_NEXT);
        }
        return;
    }
    log_msg("news: tap turns the page");
    handle_event(KHOME_EV_NEWS_NEXT);
}

static void on_tap(Tap tap)
{
    TapRect rects[16];
    int count;
    int dx;
    int dy;
    int i;

    /* Design units scale by 758/1072 on both axes. */
    dx = (int)lroundf((float)tap.x * 1072.0f / 758.0f);
    dy = (int)lroundf((float)tap.y * 1072.0f / 758.0f);
    log_msg("tap %d,%d -> design %d,%d", tap.x, tap.y, dx, dy);
    /* The launch screen has no controls of its own.  Once the browser has had
     * the normal spawn time and its window is not up, a tap is the user
     * asking to go back rather than a page tap. */
    if (state == KHOME_STATE_BROWSER && !browser_seen &&
        now_ms() - browser_since >= KHOME_BROWSER_CANCEL_GRACE_MS &&
        !window_viewable(KHOME_APP_BROWSER)) {
        browser_launch_abort("cancelled by tap", "Browser launch cancelled");
        return;
    }
    if (state == KHOME_STATE_SHEET) {
        on_sheet_tap(dx, dy);
        return;
    }
    if (state == KHOME_STATE_NEWS_MENU) {
        on_news_menu_tap(dx, dy);
        return;
    }
    if (state == KHOME_STATE_EDIT) {
        on_edit_tap(dx, dy);
        return;
    }
    if (state == KHOME_STATE_SETTINGS) {
        on_settings_tap(dx, dy);
        return;
    }
    if (state == KHOME_STATE_SETTINGS_CONFIRM ||
        state == KHOME_STATE_HOME_CONFIRM) {
        on_confirm_tap(dx, dy);
        return;
    }
    if (state == KHOME_STATE_NEWS) {
        on_news_tap(tap, dx, dy);
        return;
    }
    if (state != KHOME_STATE_HOME) {
        return;
    }
    count = layout_home_taps(&model, rects, 16);
    for (i = 0; i < count; i++) {
        if (!point_in_rect(dx, dy, &rects[i])) {
            continue;
        }
        log_msg("tap action %s", rects[i].action);
        if (strcmp(rects[i].action, "app_browser") == 0) {
            handle_event(KHOME_EV_TAP_BROWSER);
        } else if (strcmp(rects[i].action, "browser_orientation") == 0) {
            browser_orientation_toggle();
        } else if (strcmp(rects[i].action, "open_browser_add") == 0) {
            handle_event(KHOME_EV_TAP_ADD);
        } else if (strncmp(rects[i].action, "fav_open_", 9) == 0) {
            favorite_index = rects[i].index;
            handle_event(KHOME_EV_TAP_FAVORITE);
        } else if (strcmp(rects[i].action, "app_manga") == 0) {
            if (access(KHOME_KOREADER, R_OK) != 0) {
                log_msg("koreader: %s is missing, staying home", KHOME_KOREADER);
                return;
            }
            handle_event(KHOME_EV_TAP_MANGA);
        } else if (strcmp(rects[i].action, "page_prev") == 0) {
            handle_event(KHOME_EV_PAGE_PREV);
        } else if (strcmp(rects[i].action, "page_next") == 0) {
            handle_event(KHOME_EV_PAGE_NEXT);
        } else if (strcmp(rects[i].action, "app_newspaper") == 0) {
            handle_event(KHOME_EV_TAP_NEWSPAPER);
        } else if (strcmp(rects[i].action, "app_settings") == 0) {
            handle_event(KHOME_EV_TAP_SETTINGS);
        } else {
            log_msg("tap ignored (%s)", rects[i].action);
        }
        return;
    }
    log_msg("tap outside every rect");
}

/* A long press on a favorite tile asks before removing it.  Every other
 * long press is the tap it would have been. */
static void on_long_press(Tap tap)
{
    TapRect rects[16];
    int dx;
    int dy;
    int count;
    int i;

    if (state == KHOME_STATE_NEWS) {
        log_msg("long press in news opens the menu");
        handle_event(KHOME_EV_SHEET_PULL);
        return;
    }
    if (state == KHOME_STATE_HOME) {
        dx = (int)lroundf((float)tap.x * 1072.0f / 758.0f);
        dy = (int)lroundf((float)tap.y * 1072.0f / 758.0f);
        log_msg("long press %d,%d -> design %d,%d", tap.x, tap.y, dx, dy);
        count = layout_home_taps(&model, rects, 16);
        for (i = 0; i < count; i++) {
            if (!point_in_rect(dx, dy, &rects[i])) {
                continue;
            }
            if (strncmp(rects[i].action, "fav_open_", 9) == 0) {
                log_msg("long press action %s", rects[i].action);
                favorite_index = rects[i].index;
                handle_event(KHOME_EV_LONG_FAVORITE);
                return;
            }
            break;
        }
    }
    on_tap(tap);
}

static void on_power_event(const LipcEvent *ev)
{
    if (strcmp(ev->name, "goingToScreenSaver") == 0) {
        handle_event(KHOME_EV_POWER_GOING_TO_SS);
    } else if (strcmp(ev->name, "outOfScreenSaver") == 0) {
        handle_event(KHOME_EV_POWER_OUT_OF_SS);
    }
}

static void on_app_event(const LipcEvent *ev)
{
    if (strcmp(ev->name, "processExited") != 0 &&
        strcmp(ev->name, "appStopped") != 0) {
        return;
    }
    if ((state != KHOME_STATE_BROWSER && state != KHOME_STATE_SHEET &&
         state != KHOME_STATE_EDIT) ||
        !browser_seen) {
        return;
    }
    if (browser_event_args(ev) && !browser_running()) {
        /* appmgrd sent processExited for the browser.  It can arrive late (a
         * kill at 06:56:52 was logged here at 06:57:31, after the next launch),
         * so the process check decides whether this is the current one. */
        log_msg("appmgrd: %s for the browser", ev->name);
        browser_exited();
    }
}

static void on_signal(int sig)
{
    unsigned char byte = 'c';

    if (sig == SIGUSR1) {
        close_requested = 1;
        byte = 'u';
    } else if (sig != SIGCHLD) {
        quit = 1;
        byte = 'q';
    }
    if (sig_pipe[1] >= 0) {
        ssize_t ignored = write(sig_pipe[1], &byte, 1);
        (void)ignored;
    }
}

static void drain_signal_pipe(void)
{
    unsigned char buf[32];

    while (read(sig_pipe[0], buf, sizeof(buf)) > 0) {
        /* The flags and waitpid below carry the information. */
    }
}

static void reap_children(void)
{
    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) {
            return;
        }
        if (pid == koreader_pid) {
            log_msg("koreader: pid %d exited (status %d)", (int)pid,
                    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            koreader_pid = 0;
            resume_volumd();
            handle_event(KHOME_EV_KOREADER_EXIT);
        } else if (pid == band_pid) {
            log_msg("band: exited (status %d)",
                    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            band_pid = 0;
        } else if (pid == shield_pid) {
            log_msg("shield: exited (status %d)",
                    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            shield_pid = 0;
        } else if (pid == shield_old_pid) {
            int keep = shield_pid > 0 ? shield_height : 0;

            log_msg("shield: old exited (status %d)",
                    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            shield_old_pid = 0;
            shield_old_due = 0;
            if (shield_old_height > keep) {
                shield_expose_band(keep, shield_old_height);
            }
            shield_old_height = 0;
        } else if (pid == fetch_pid) {
            news_fetch_done(status);
        } else if (pid == power_watch.pid) {
            log_msg("powerd event child died");
            power_watch.pid = 0;
            if (power_watch.fd >= 0) {
                close(power_watch.fd);
                power_watch.fd = -1;
            }
        } else if (pid == app_watch.pid) {
            log_msg("appmgrd event child died");
            app_watch.pid = 0;
            if (app_watch.fd >= 0) {
                close(app_watch.fd);
                app_watch.fd = -1;
            }
        }
    }
}

/* A watcher that dies at once must not cost a fork every poll tick. */
static int watch_retry_due(int64_t *last)
{
    int64_t now = now_ms();

    if (*last != 0 && now - *last < KHOME_WATCH_RETRY_MS) {
        return 0;
    }
    *last = now;
    return 1;
}

static void ensure_watches(void)
{
    if (power_watch.fd < 0 && watch_retry_due(&power_watch_try)) {
        if (lipc_watch_start(&power_watch, "com.lab126.powerd",
                             "goingToScreenSaver,outOfScreenSaver") < 0) {
            log_msg("powerd watch restart failed");
        } else {
            log_msg("powerd watch started");
        }
    }
    if (app_watch.fd < 0 && watch_retry_due(&app_watch_try)) {
        if (lipc_watch_start(&app_watch, "com.lab126.appmgrd", "*") < 0) {
            log_msg("appmgrd watch restart failed");
        } else {
            log_msg("appmgrd watch started");
        }
    }
}

/* xband dies at once if the browser is not up yet.  The retry clock keeps a
 * crash loop from forking every poll tick while still bringing the strip back
 * once the interval has passed. */
static void ensure_band(void)
{
    if (band_pid <= 0 && band_state(state) && watch_retry_due(&band_try)) {
        band_start();
    }
}

static void ensure_shield(void)
{
    int height = shield_target_height(state);

    if (shield_pid <= 0 && height > 0 && watch_retry_due(&shield_try)) {
        shield_start(height);
    }
}

static void check_wifi_poll(void)
{
    HomeModel next;

    if (wifi_poll_left <= 0 || now_ms() < wifi_poll_due) {
        return;
    }
    if (asleep) {
        return;
    }
    wifi_poll_left--;
    wifi_poll_due = now_ms() + KHOME_WIFI_POLL_MS;
    next = model;
    model_update(&next);
    if (!model_status_changed(&model, &next)) {
        return;
    }
    model = next;
    if (state == KHOME_STATE_SETTINGS || state == KHOME_STATE_SETTINGS_CONFIRM) {
        settings.home = model;
        log_msg("settings: wifi status caught up");
        draw_frame_ex(1, 0);
    }
}

static void check_minute(void)
{
    time_t now = time(NULL);
    time_t minute = now / 60;

    if (minute == last_minute) {
        return;
    }
    last_minute = minute;
    /* The saver owns the panel; the wake redraw pulls the clock and status
     * fresh, so a clock-only refresh here would paint over it. */
    if (asleep) {
        return;
    }
    {
        HomeModel next = model;
        int changed;
        int day_changed;
        model_update(&next);
        changed = model_status_changed(&model, &next);
        day_changed = next.day_of_year != model.day_of_year;
        model = next;
        if (day_changed) {
            handle_event(KHOME_EV_DAY_CHANGED);
        } else {
            handle_event(changed ? KHOME_EV_STATUS_CHANGED
                                 : KHOME_EV_MINUTE);
        }
    }
}

/* A launch that never put the browser on screen has to give the panel back.
 * Stop a browser that did start so it cannot repaint over home, then tap away
 * a crash alert before home is drawn under it. */
static void browser_launch_abort(const char *why, const char *notice)
{
    int64_t age = now_ms() - browser_since;

    log_msg("browser: launch %s after %d ms", why, (int)age);
    snprintf(model.notice, sizeof(model.notice), "%s", notice);
    shield_stop();
    if (browser_running()) {
        browser_stop();
    } else if (window_viewable("pillowAlert")) {
        dismiss_alert();
    }
    wait_for_portrait();
    handle_event(KHOME_EV_BROWSER_EXIT);
}

/* appmgrd's own exit event and the process list are redundant; whichever
 * arrives first takes us home.  Until the browser's window is viewable the
 * start deadline applies: a mesquite process alone is not proof the browser
 * came up, since every WAF app runs under that comm. */
static void check_browser_exit(void)
{
    int64_t age;

    if (state != KHOME_STATE_BROWSER && state != KHOME_STATE_SHEET &&
        state != KHOME_STATE_EDIT) {
        return;
    }
    age = now_ms() - browser_since;
    if (browser_running()) {
        if (browser_seen) {
            return;
        }
        if (window_viewable(KHOME_APP_BROWSER)) {
            browser_seen = 1;
            log_msg("browser: in front after %d ms", (int)age);
            return;
        }
    } else if (browser_seen) {
        log_msg("browser: process gone after %d ms", (int)age);
        browser_exited();
        return;
    }
    if (!browser_seen && age >= KHOME_BROWSER_START_TIMEOUT_MS) {
        browser_launch_abort("did not reach the screen",
                             "Browser did not start");
    }
}

static int poll_timeout_ms(void)
{
    int64_t now = now_ms();
    int64_t timeout = KHOME_LONG_POLL_MS;

    if (defer_active) {
        int64_t left = defer_due - now;
        if (left < 0) {
            left = 0;
        }
        if (left < timeout) {
            timeout = left;
        }
    }
    if (shield_old_pid > 0) {
        int64_t left = shield_old_due - now;
        if (left < 0) {
            left = 0;
        }
        if (left < timeout) {
            timeout = left;
        }
    }
    if (timeout < 0) {
        timeout = 0;
    }
    return (int)timeout;
}

int main(int argc, char **argv)
{
    const char *font_dir = KHOME_FONT_DIR;
    const char *icon_dir = KHOME_ICON_DIR;
    struct sigaction sa;
    int status = 0;

    (void)argc;
    (void)argv;
    fb.fd = -1;
    touch.fd = -1;
    power_watch.fd = -1;
    app_watch.fd = -1;
    setenv("TZ", "JST-9", 1);
    tzset();
    lock_fd_close_on_exec();

    if (log_init() < 0) {
        return 1;
    }
    log_msg("khome: start");
    if (access(KHOME_DISABLE_FILE, F_OK) == 0) {
        log_msg("khome: %s exists, exiting", KHOME_DISABLE_FILE);
        fclose(log_file);
        return 0;
    }
    if (getenv("KHOME_FONTS") != NULL) {
        font_dir = getenv("KHOME_FONTS");
    }
    if (getenv("KHOME_ICONS") != NULL) {
        icon_dir = getenv("KHOME_ICONS");
    }

    memset(&model, 0, sizeof(model));
    model.battery_level = -1;
    if (!fonts_load(&fonts, font_dir)) {
        log_msg("khome: cannot load fonts from %s", font_dir);
        fclose(log_file);
        return 1;
    }
    if (!icons_load(&icons, icon_dir)) {
        log_msg("khome: cannot load icons from %s", icon_dir);
        fonts_free(&fonts);
        fclose(log_file);
        return 1;
    }
    if (!canvas_init(&canvas, 758, 1024)) {
        log_msg("khome: canvas allocation failed");
        icons_free(&icons);
        fonts_free(&fonts);
        fclose(log_file);
        return 1;
    }
    if (fb_open(&fb) < 0) {
        log_msg("khome: framebuffer unavailable");
        canvas_free(&canvas);
        icons_free(&icons);
        fonts_free(&fonts);
        fclose(log_file);
        return 1;
    }
    if (input_open(&touch) < 0) {
        log_msg("khome: touch device unavailable");
        fb_close(&fb);
        canvas_free(&canvas);
        icons_free(&icons);
        fonts_free(&fonts);
        fclose(log_file);
        return 1;
    }
    log_msg("khome: touch %s, fb %dx%d", touch.path, fb.width, fb.height);
    gesture_init(&gesture);
    if (touch.has_initial) {
        gesture_set_position(&gesture, touch.initial_x, touch.initial_y);
        log_msg("touch: initial position %d,%d", touch.initial_x,
                touch.initial_y);
    }

    if (pipe(sig_pipe) < 0) {
        log_msg("khome: pipe failed");
        status = 1;
        goto cleanup;
    }
    {
        int flags = fcntl(sig_pipe[0], F_GETFL, 0);
        fcntl(sig_pipe[0], F_SETFL, flags | O_NONBLOCK);
        flags = fcntl(sig_pipe[1], F_GETFL, 0);
        fcntl(sig_pipe[1], F_SETFL, flags | O_NONBLOCK);
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);

    ensure_watches();
    /* khome.sh restarts a khome that died by signal, possibly while the
     * browser was in front; home must not draw under a live browser. */
    if (browser_running()) {
        log_msg("khome: browser left running, stopping it");
        browser_stop();
    }
    last_minute = time(NULL) / 60;
    model_update(&model);
    read_favorites(&model);
    if (browser_orientation_refresh() < 0) {
        log_msg("browser: settings unreadable, showing portrait");
    }
    news_text_size_load();
    handle_event(KHOME_EV_START);

    while (!quit) {
        struct pollfd fds[4];
        int nfds = 0;
        int touch_idx = -1;
        int power_idx = -1;
        int app_idx = -1;
        int sig_idx;
        int ready;

        sig_idx = nfds;
        fds[nfds].fd = sig_pipe[0];
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        nfds++;
        if (touch.fd >= 0) {
            touch_idx = nfds;
            fds[nfds].fd = touch.fd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            nfds++;
        }
        if (power_watch.fd >= 0) {
            power_idx = nfds;
            fds[nfds].fd = power_watch.fd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            nfds++;
        }
        if (app_watch.fd >= 0) {
            app_idx = nfds;
            fds[nfds].fd = app_watch.fd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            nfds++;
        }

        ready = poll(fds, (nfds_t)nfds, poll_timeout_ms());
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_msg("khome: poll failed: %s", strerror(errno));
            status = 1;
            break;
        }

        if (fds[sig_idx].revents != 0) {
            drain_signal_pipe();
            reap_children();
        }
        if (quit) {
            break;
        }
        shield_old_tick();
        if (close_requested) {
            close_requested = 0;
            handle_event(KHOME_EV_BROWSER_CLOSE);
        }
        if (touch_idx >= 0 && fds[touch_idx].revents != 0 && asleep) {
            /* The saver is up; drop the touch and any gesture it began. */
            input_drain(&touch, &gesture);
            gesture_cancel(&gesture);
        } else if (touch_idx >= 0 && fds[touch_idx].revents != 0) {
            Tap tap;
            for (;;) {
                int was_down = gesture.down;
                GestureResult result = GESTURE_RESULT_NONE;
                int landscape = screen_landscape();
                int got = input_poll_gesture_oriented(&touch, &gesture, &tap,
                                                      &result, landscape);
                if (got < 0) {
                    log_msg("khome: touch read failed");
                    break;
                }
                if (got > 0) {
                    if (result == GESTURE_RESULT_SHEET_PULL) {
                        if (state == KHOME_STATE_BROWSER && !browser_seen &&
                            now_ms() - browser_since >=
                                KHOME_BROWSER_CANCEL_GRACE_MS &&
                            !window_viewable(KHOME_APP_BROWSER)) {
                            browser_launch_abort("cancelled by swipe",
                                                 "Browser launch cancelled");
                        } else if (screen_landscape()) {
                            /* There is no usable portrait sheet over a
                             * landscape browser: the same gesture goes home
                             * through the sheet's Home path. */
                            log_msg("browser: top pull in landscape, closing");
                            handle_event(KHOME_EV_BROWSER_CLOSE);
                        } else {
                            handle_event(KHOME_EV_SHEET_PULL);
                        }
                    } else if (result == GESTURE_RESULT_SWIPE_LEFT) {
                        handle_event(KHOME_EV_NEWS_NEXT);
                    } else if (result == GESTURE_RESULT_SWIPE_RIGHT) {
                        handle_event(KHOME_EV_NEWS_PREV);
                    } else if (result == GESTURE_RESULT_LONG_PRESS) {
                        on_long_press(tap);
                    } else {
                        on_tap(tap);
                    }
                } else if (was_down && !gesture.down) {
                    log_msg("touch release without tap (start %d,%d end %d,%d)",
                            gesture.start_x, gesture.start_y, gesture.last_x,
                            gesture.last_y);
                }
                if (!was_down && gesture.down) {
                    log_msg("touch press");
                }
                if (got == 0) {
                    break;
                }
            }
        }
        if (power_idx >= 0 && fds[power_idx].revents != 0) {
            LipcEvent ev;
            int got;
            while ((got = lipc_watch_read(&power_watch, &ev)) == 1) {
                log_msg("powerd: %s", ev.name);
                on_power_event(&ev);
            }
            if (got < 0) {
                lipc_watch_stop(&power_watch);
            }
        }
        if (app_idx >= 0 && fds[app_idx].revents != 0) {
            LipcEvent ev;
            int got;
            while ((got = lipc_watch_read(&app_watch, &ev)) == 1) {
                on_app_event(&ev);
            }
            if (got < 0) {
                lipc_watch_stop(&app_watch);
            }
        }
        ensure_watches();
        ensure_band();
        ensure_shield();

        if (defer_active && now_ms() >= defer_due) {
            defer_active = 0;
            handle_event(defer_event);
        }
        check_minute();
        check_wifi_poll();
        check_browser_exit();
    }

cleanup:
    if (quit) {
        log_msg("khome: quit requested");
    }
    if (quit_code != 0) {
        status = quit_code;
    }
    /* The shield covers the alert's close button, so it goes before any
     * browser stop that may have to tap it. */
    shield_stop();
    /* Quitting with the browser up must not leave restore() to drop it with
     * the kill property: appmgrd reads that as a failed app and raises its
     * "Application Error" alert over the stock UI.  Stop it the way the close
     * path does, while fakekaf can still answer as the home booklet. */
    if (browser_running()) {
        browser_stop();
    } else if (browser_seen && (state == KHOME_STATE_BROWSER ||
                                state == KHOME_STATE_SHEET ||
                                state == KHOME_STATE_EDIT)) {
        /* It crashed after the last poll, so check_browser_exit() never
         * closed the alert appmgrd raises for it. */
        dismiss_alert();
    }
    koreader_stop();
    grab_touch(0);
    band_stop();
    lipc_watch_stop(&power_watch);
    lipc_watch_stop(&app_watch);
    input_close(&touch);
    fb_close(&fb);
    canvas_free(&canvas);
    icons_free(&icons);
    fonts_free(&fonts);
    log_msg("khome: exit %d", status);
    fclose(log_file);
    return status;
}
