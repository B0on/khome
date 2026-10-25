/* Asks X to repaint a rectangle of the screen.  The sheet is drawn straight
 * into /dev/fb0, so the browser underneath has no damage and will not repaint
 * on its own.  Mapping an override-redirect window over the rectangle and
 * unmapping it makes X send Expose to whatever it covered.
 *
 * Xlib is loaded with dlopen so this builds with the same sysroot as fakekaf
 * and needs no X11 headers or link-time library.  The declarations below are
 * the stable, public part of Xlib and X.h.
 *
 * This deliberately avoids stdout/stderr and static pointer arrays: lld emits
 * an R_ARM_RELATIVE with a nonzero symbol index for those, which the device's
 * glibc 2.20 loader crashes on.  A binary with no dynamic relative relocation
 * runs, as fakekaf does. */

#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct _XDisplay Display;
typedef unsigned long Window;
typedef unsigned long Pixmap;
typedef unsigned long Cursor;
typedef unsigned long Colormap;

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

#define CW_OVERRIDE_REDIRECT (1L << 9)
#define XEXPOSE_HOLD_NS 60000000L

static int fail(const char *what)
{
    write(2, "xexpose: ", 9);
    write(2, what, strlen(what));
    write(2, "\n", 1);
    return 1;
}

int main(int argc, char **argv)
{
    void *lib;
    Display *(*open_display)(const char *);
    int (*close_display)(Display *);
    Window (*root_window)(Display *, int);
    Window (*create_simple)(Display *, Window, int, int, unsigned, unsigned,
                            unsigned, unsigned long, unsigned long);
    int (*change_attrs)(Display *, Window, unsigned long,
                        XSetWindowAttributes *);
    int (*map_window)(Display *, Window);
    int (*unmap_window)(Display *, Window);
    int (*destroy_window)(Display *, Window);
    int (*sync)(Display *, int);
    Display *dpy;
    struct timespec hold;
    XSetWindowAttributes attrs;
    Window win;
    int x;
    int y;
    int w;
    int h;

    if (argc != 5) {
        return fail("usage: xexpose x y w h");
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
    LOAD(create_simple, "XCreateSimpleWindow");
    LOAD(change_attrs, "XChangeWindowAttributes");
    LOAD(map_window, "XMapWindow");
    LOAD(unmap_window, "XUnmapWindow");
    LOAD(destroy_window, "XDestroyWindow");
    LOAD(sync, "XSync");
#undef LOAD

    dpy = open_display(NULL);
    if (dpy == NULL) {
        return fail("cannot open DISPLAY");
    }
    win = create_simple(dpy, root_window(dpy, 0), x, y, (unsigned)w,
                        (unsigned)h, 0, 0, 0);
    if (win == 0) {
        close_display(dpy);
        return fail("cannot create the window");
    }
    attrs.override_redirect = 1;
    change_attrs(dpy, win, CW_OVERRIDE_REDIRECT, &attrs);
    map_window(dpy, win);
    sync(dpy, 0);
    hold.tv_sec = 0;
    hold.tv_nsec = XEXPOSE_HOLD_NS;
    nanosleep(&hold, NULL);
    unmap_window(dpy, win);
    destroy_window(dpy, win);
    sync(dpy, 0);
    close_display(dpy);
    write(1, "exposed\n", 8);
    return 0;
}
