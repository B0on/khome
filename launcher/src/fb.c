#define _POSIX_C_SOURCE 200809L

#include "fb.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define KHOME_FB_PATH "/dev/fb0"
#define KHOME_FBINK "/mnt/us/usbnet/bin/fbink"

static int run_fbink(char *const argv[])
{
    pid_t pid;
    int status;

    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
        }
        execv(KHOME_FBINK, argv);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }
    return 0;
}

int fb_open(Fb *fb)
{
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    int y;

    if (fb == NULL) {
        return -1;
    }
    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;
    fb->fd = open(KHOME_FB_PATH, O_RDWR);
    if (fb->fd < 0) {
        fprintf(stderr, "khome: cannot open %s: %s\n", KHOME_FB_PATH,
                strerror(errno));
        return -1;
    }
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &var) < 0 ||
        ioctl(fb->fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        fprintf(stderr, "khome: cannot read fb info: %s\n", strerror(errno));
        close(fb->fd);
        fb->fd = -1;
        return -1;
    }
    if (var.bits_per_pixel != 8 || var.grayscale == 0) {
        fprintf(stderr, "khome: unsupported fb: %d bpp, grayscale %d\n",
                var.bits_per_pixel, var.grayscale);
        close(fb->fd);
        fb->fd = -1;
        return -1;
    }
    fb->width = (int)var.xres;
    fb->height = (int)var.yres;
    fb->line_length = (int)fix.line_length;
    fb->mem_size = fix.smem_len;
    fb->mem = mmap(NULL, fb->mem_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fb->fd, 0);
    if (fb->mem == MAP_FAILED) {
        fprintf(stderr, "khome: cannot mmap fb: %s\n", strerror(errno));
        fb->mem = NULL;
        close(fb->fd);
        fb->fd = -1;
        return -1;
    }
    /* The 10 bytes past xres pad each visible row; leave them white so a
     * refresh never picks up stale black padding. */
    for (y = 0; y < fb->height; y++) {
        memset(fb->mem + (size_t)y * (size_t)fb->line_length + fb->width, 255,
               (size_t)(fb->line_length - fb->width));
    }
    return 0;
}

void fb_close(Fb *fb)
{
    if (fb == NULL) {
        return;
    }
    if (fb->mem != NULL) {
        munmap(fb->mem, fb->mem_size);
        fb->mem = NULL;
    }
    if (fb->fd >= 0) {
        close(fb->fd);
        fb->fd = -1;
    }
}

int fb_refresh_geometry(Fb *fb)
{
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    unsigned char *mem;
    size_t old_size;

    if (fb == NULL || fb->fd < 0) {
        return -1;
    }
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &var) < 0 ||
        ioctl(fb->fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        return -1;
    }
    if ((size_t)fix.smem_len != fb->mem_size || fb->mem == NULL) {
        old_size = fb->mem_size;
        mem = mmap(NULL, (size_t)fix.smem_len, PROT_READ | PROT_WRITE,
                   MAP_SHARED, fb->fd, 0);
        if (mem == MAP_FAILED) {
            return -1;
        }
        if (fb->mem != NULL) {
            munmap(fb->mem, old_size);
        }
        fb->mem = mem;
        fb->mem_size = (size_t)fix.smem_len;
    }
    fb->width = (int)var.xres;
    fb->height = (int)var.yres;
    fb->line_length = (int)fix.line_length;
    if (fb->line_length > fb->width) {
        int y;

        /* Returning from landscape leaves the portrait rows' padding as it
         * was when the panel rotated, so put it back to white like fb_open(). */
        for (y = 0; y < fb->height; y++) {
            memset(fb->mem + (size_t)y * (size_t)fb->line_length + fb->width,
                   255, (size_t)(fb->line_length - fb->width));
        }
    }
    return 0;
}

int fb_blit(Fb *fb, const Canvas *canvas)
{
    int y;

    if (fb == NULL || fb->mem == NULL || canvas == NULL ||
        canvas->pixels == NULL) {
        return -1;
    }
    if (fb_refresh_geometry(fb) < 0) {
        return -1;
    }
    if (canvas->w != fb->width || canvas->h != fb->height) {
        return -1;
    }
    for (y = 0; y < fb->height; y++) {
        memcpy(fb->mem + (size_t)y * (size_t)fb->line_length,
               canvas->pixels + (size_t)y * (size_t)canvas->stride,
               (size_t)fb->width);
    }
    return 0;
}

int fb_blit_rect(Fb *fb, const Canvas *canvas, int x, int y, int w, int h)
{
    int row;

    if (fb == NULL || fb->mem == NULL || canvas == NULL ||
        canvas->pixels == NULL) {
        return -1;
    }
    if (fb_refresh_geometry(fb) < 0) {
        return -1;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > fb->width || y + h > fb->height || canvas->w != fb->width ||
        canvas->h != fb->height) {
        return -1;
    }
    if (w <= 0 || h <= 0) {
        return -1;
    }
    for (row = y; row < y + h; row++) {
        memcpy(fb->mem + (size_t)row * (size_t)fb->line_length + (size_t)x,
               canvas->pixels + (size_t)row * (size_t)canvas->stride +
                   (size_t)x,
               (size_t)w);
    }
    return 0;
}

int fb_refresh_full(Fb *fb)
{
    return fb_refresh_full_wave(fb, "GC16");
}

int fb_refresh_full_wave(Fb *fb, const char *wave)
{
    char *argv[] = { "fbink", "-q", "-s", "-W", (char *)wave, "-w", NULL };

    (void)fb;
    return run_fbink(argv);
}

int fb_refresh_rect(Fb *fb, int x, int y, int w, int h)
{
    return fb_refresh_rect_wave(fb, x, y, w, h, "GC16");
}

int fb_refresh_rect_wave(Fb *fb, int x, int y, int w, int h,
                         const char *wave)
{
    char rect[64];
    char *argv[] = { "fbink", "-q", "-s", rect, "-W", (char *)wave, "-w", NULL };

    if (fb == NULL) {
        return -1;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > fb->width) {
        w = fb->width - x;
    }
    if (y + h > fb->height) {
        h = fb->height - y;
    }
    if (w <= 0 || h <= 0) {
        return -1;
    }
    snprintf(rect, sizeof(rect), "top=%d,left=%d,width=%d,height=%d", y, x, w,
             h);
    return run_fbink(argv);
}
