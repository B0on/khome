/* Holds a window over the browser in front of khome.  Two uses:
 *
 * The default mode is an invisible input-only strip over the top edge of the
 * screen.  khome keeps reading /dev/input/event1 without grabbing it, so the
 * browser would otherwise get the same top-edge swipe and scroll the page
 * under the tab sheet.  A press on this override-redirect window gives X an
 * implicit grab, so the motion and the release of that touch stay with this
 * window and never reach the browser.
 *
 * The "shield" mode is an input-output window with no background, mapped over
 * the tab sheet's rectangle while the sheet is open.  khome draws the sheet
 * straight into /dev/fb0, so a browser repaint during a page load would paint
 * over it.  As a sibling above the browser this window takes the rectangle out
 * of X's clipping, so the browser cannot draw there; with the background left
 * as None the server paints nothing, and the sheet's own fb bytes stay.
 *
 * Xlib is loaded with dlopen for the same reason xexpose does: the device's
 * glibc 2.20 loader crashes on a binary whose .rel.dyn holds R_ARM_RELATIVE
 * entries.  No stdout/stderr globals, only write(2) on failure.
 *
 * The window is kept on top with a periodic XRaiseWindow: the browser maps its
 * own window after khome starts it, and a mapped sibling would otherwise sit
 * above a window that was mapped earlier. */

#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct _XDisplay Display;
typedef unsigned long Window;
typedef unsigned long Pixmap;
typedef unsigned long Cursor;
typedef unsigned long Colormap;
typedef struct _XVisual Visual;

typedef struct {
    Pixmap background_pixmap;
    unsigned long background_pixel;
    Pixmap border_pixmap;
    unsigned long border_pixel;
    int bit_gravity;
    int win_gravity;
    int backing_store;
    unsigned long backing_planes;
    unsigned long backing_pixel;
    int save_under;
    long event_mask;
    long do_not_propagate_mask;
    int override_redirect;
    Colormap colormap;
    Cursor cursor;
} XSetWindowAttributes;

typedef struct {
    int type;
    unsigned long serial;
    int send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    unsigned long time;
    int x;
    int y;
    int x_root;
    int y_root;
    unsigned int state;
    unsigned int button;
    int same_screen;
} XButtonEvent;

typedef union {
    int type;
    XButtonEvent button;
    long pad[24];
} XEvent;

#define INPUT_OUTPUT 1
#define INPUT_ONLY 2
#define CW_BACK_PIXMAP (1L << 0)
#define CW_EVENT_MASK (1L << 11)
#define CW_OVERRIDE_REDIRECT (1L << 9)
/* X11/X.h values; Xlib is dlopen'ed, so the header's names are not visible. */
#define BUTTON_PRESS_MASK (1L << 2)
#define BUTTON_RELEASE_MASK (1L << 3)
#define BUTTON_MOTION_MASK (1L << 13)
#define XBAND_RAISE_NS 100000000L

static volatile sig_atomic_t stop_requested;

static int fail(const char *what)
{
    write(2, "xband: ", 7);
    write(2, what, strlen(what));
    write(2, "\n", 1);
    return 1;
}

static void on_signal(int sig)
{
    (void)sig;
    stop_requested = 1;
}

int main(int argc, char **argv)
{
    void *lib;
    Display *(*open_display)(const char *);
    int (*close_display)(Display *);
    Window (*root_window)(Display *, int);
    Window (*create_window)(Display *, Window, int, int, unsigned, unsigned,
                            unsigned, int, unsigned, Visual *, unsigned long,
                            XSetWindowAttributes *);
    int (*map_window)(Display *, Window);
    int (*raise_window)(Display *, Window);
    int (*destroy_window)(Display *, Window);
    int (*sync)(Display *, int);
    int (*flush)(Display *);
    int (*pending)(Display *);
    int (*next_event)(Display *, XEvent *);
    struct sigaction sa;
    struct timespec pause;
    XSetWindowAttributes attrs;
    XEvent ev;
    Display *dpy;
    Window win;
    int shield = 0;
    int x;
    int y;
    int w;
    int h;

    if (argc != 5 && argc != 6) {
        return fail("usage: xband x y w h [shield]");
    }
    if (argc == 6) {
        if (strcmp(argv[5], "shield") != 0) {
            return fail("usage: xband x y w h [shield]");
        }
        shield = 1;
    }
    x = atoi(argv[1]);
    y = atoi(argv[2]);
    w = atoi(argv[3]);
    h = atoi(argv[4]);
    if (w <= 0 || h <= 0) {
        return fail("empty rectangle");
    }
    lib = dlopen("libX11.so.6", RTLD_NOW);
    if (lib == NULL) {
        return fail("cannot load libX11.so.6");
    }
#define LOAD(var, name)                                                        \
    do {                                                                       \
        void *sym = dlsym(lib, name);                                          \
        if (sym == NULL) {                                                     \
            return fail("libX11 is missing " name);                            \
        }                                                                      \
        memcpy(&var, &sym, sizeof(sym));                                       \
    } while (0)
    LOAD(open_display, "XOpenDisplay");
    LOAD(close_display, "XCloseDisplay");
    LOAD(root_window, "XRootWindow");
    LOAD(create_window, "XCreateWindow");
    LOAD(map_window, "XMapWindow");
    LOAD(raise_window, "XRaiseWindow");
    LOAD(destroy_window, "XDestroyWindow");
    LOAD(sync, "XSync");
    LOAD(flush, "XFlush");
    LOAD(pending, "XPending");
    LOAD(next_event, "XNextEvent");
#undef LOAD

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    dpy = open_display(NULL);
    if (dpy == NULL) {
        return fail("cannot open DISPLAY");
    }
    memset(&attrs, 0, sizeof(attrs));
    attrs.override_redirect = 1;
    if (shield) {
        /* No background and no exposure: mapping the window must not paint
         * anything, so the sheet already in the framebuffer shows through. */
        attrs.background_pixmap = 0;
        win = create_window(dpy, root_window(dpy, 0), x, y, (unsigned)w,
                            (unsigned)h, 0, 0, INPUT_OUTPUT, NULL,
                            CW_OVERRIDE_REDIRECT | CW_BACK_PIXMAP, &attrs);
    } else {
        attrs.event_mask = BUTTON_PRESS_MASK | BUTTON_RELEASE_MASK |
                           BUTTON_MOTION_MASK;
        win = create_window(dpy, root_window(dpy, 0), x, y, (unsigned)w,
                            (unsigned)h, 0, 0, INPUT_ONLY, NULL,
                            CW_OVERRIDE_REDIRECT | CW_EVENT_MASK, &attrs);
    }
    if (win == 0) {
        close_display(dpy);
        return fail("cannot create the window");
    }
    map_window(dpy, win);
    /* Raise before the first sleep, so no frame is left uncovered. */
    raise_window(dpy, win);
    sync(dpy, 0);

    pause.tv_sec = 0;
    pause.tv_nsec = XBAND_RAISE_NS;
    while (!stop_requested) {
        /* The press and its implicit-grab events stay queued here, so drain
         * them and never let the connection's queue grow. */
        while (pending(dpy) > 0) {
            next_event(dpy, &ev);
        }
        raise_window(dpy, win);
        flush(dpy);
        nanosleep(&pause, NULL);
        /* khome is the parent; if it dies without stopping us, the strip must
         * not outlive it over the stock UI. */
        if (getppid() == 1) {
            break;
        }
    }
    destroy_window(dpy, win);
    sync(dpy, 0);
    close_display(dpy);
    return 0;
}
