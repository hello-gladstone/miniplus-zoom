/*
 * zoom_daemon.c — Pixel Zoom Inspector for Miyoo Mini Plus / Onion OS
 * v7 — queries active framebuffer on every zoom (fixes works-only-once bug)
 *
 * Press MENU + L1  → freeze game and zoom in (toggle ON)
 * D-pad            → pan around the frozen frame
 * Press MENU + L1  → back to normal, game resumes (toggle OFF)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>

#define SCREEN_W      640
#define SCREEN_H      480
#define BYTES_PER_PIX 4
#define FB_SIZE       (SCREEN_W * SCREEN_H * BYTES_PER_PIX)
/* Map enough for up to 3 buffers to be safe */
#define FB_MAP_SIZE   (FB_SIZE * 3)

/* 5x zoom: 128x96 region scaled to 640x480 */
#define ZOOM_W        128
#define ZOOM_H        96
#define PAN_STEP      16

#define KEY_MIYOO_MENU   1
#define KEY_MIYOO_L1     18
#define KEY_DPAD_UP      103
#define KEY_DPAD_DOWN    108
#define KEY_DPAD_LEFT    105
#define KEY_DPAD_RIGHT   106

#define INPUT_DEV "/dev/input/event0"
#define FB_DEV    "/dev/fb0"

static int        fb_fd    = -1;
static uint32_t  *fb_full  = NULL;   /* whole mapped framebuffer       */
static size_t     fb_map   = 0;      /* how much we actually mapped    */
static uint32_t  *fb_mem   = NULL;   /* points at the ACTIVE buffer    */
static uint32_t  *fb_back  = NULL;   /* our snapshot                   */

static int   zoom_active = 0;
static int   menu_held   = 0;
static int   zoom_x      = (SCREEN_W - ZOOM_W) / 2;
static int   zoom_y      = (SCREEN_H - ZOOM_H) / 2;
static pid_t paused_pid  = 0;

static inline int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static pid_t find_emulator(void) {
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *e;
    pid_t found = 0;
    char path[64], name[128];
    while ((e = readdir(d)) != NULL && !found) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        snprintf(path, sizeof(path), "/proc/%s/comm", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (fgets(name, sizeof(name), f)) {
            name[strcspn(name, "\n")] = 0;
            if (strstr(name, "retroarch") || strstr(name, "ra32") ||
                strstr(name, "ra64"))
                found = (pid_t)atoi(e->d_name);
        }
        fclose(f);
    }
    closedir(d);
    return found;
}

static int fb_init(void) {
    fb_fd = open(FB_DEV, O_RDWR);
    if (fb_fd < 0) { perror("open fb0"); return -1; }

    /* Find out how big the framebuffer really is */
    struct fb_fix_screeninfo finfo;
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) == 0 && finfo.smem_len > 0)
        fb_map = finfo.smem_len;
    else
        fb_map = FB_MAP_SIZE;

    if (fb_map > FB_MAP_SIZE) fb_map = FB_MAP_SIZE;

    fb_full = (uint32_t *)mmap(NULL, fb_map, PROT_READ|PROT_WRITE,
                               MAP_SHARED, fb_fd, 0);
    if (fb_full == MAP_FAILED) { perror("mmap"); close(fb_fd); return -1; }

    fb_back = (uint32_t *)malloc(FB_SIZE);
    if (!fb_back) { perror("malloc"); munmap(fb_full, fb_map); close(fb_fd); return -1; }
    return 0;
}

/*
 * Point fb_mem at whichever buffer is being DISPLAYED right now.
 * We ask the kernel via FBIOGET_VSCREENINFO — yoffset tells us
 * how many rows down the active buffer starts.
 */
static void locate_active_buffer(void) {
    struct fb_var_screeninfo vinfo;
    uint32_t offset_px = 0;
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) == 0)
        offset_px = vinfo.yoffset * SCREEN_W;

    /* Safety: don't point past what we mapped */
    if ((offset_px + SCREEN_W * SCREEN_H) * BYTES_PER_PIX > fb_map)
        offset_px = 0;

    fb_mem = fb_full + offset_px;
}

static void save_screen(void)    { memcpy(fb_back, fb_mem, FB_SIZE); }
static void restore_screen(void) { memcpy(fb_mem, fb_back, FB_SIZE); }

static void draw_zoom(void) {
    for (int oy = 0; oy < SCREEN_H; oy++) {
        int sy = clamp(zoom_y + (oy * ZOOM_H) / SCREEN_H, 0, SCREEN_H-1);
        for (int ox = 0; ox < SCREEN_W; ox++) {
            int sx = clamp(zoom_x + (ox * ZOOM_W) / SCREEN_W, 0, SCREEN_W-1);
            fb_mem[oy * SCREEN_W + ox] = fb_back[sy * SCREEN_W + sx];
        }
    }
}

static void enter_zoom(void) {
    if (zoom_active) return;

    /* 1. Freeze the emulator */
    paused_pid = find_emulator();
    if (paused_pid > 0) kill(paused_pid, SIGSTOP);
    usleep(30000);

    /* 2. Find which buffer is actually on screen RIGHT NOW */
    locate_active_buffer();

    /* 3. Snapshot and zoom */
    zoom_active = 1;
    zoom_x = (SCREEN_W - ZOOM_W) / 2;
    zoom_y = (SCREEN_H - ZOOM_H) / 2;
    save_screen();
    draw_zoom();
}

static void exit_zoom(void) {
    if (!zoom_active) return;
    zoom_active = 0;
    restore_screen();
    if (paused_pid > 0) {
        kill(paused_pid, SIGCONT);
        paused_pid = 0;
    }
}

static void toggle_zoom(void) {
    if (zoom_active) exit_zoom();
    else enter_zoom();
}

static void handle_key(int code, int value) {
    if (value == 2) return;
    int p = (value == 1);

    if (code == KEY_MIYOO_MENU) {
        menu_held = p;
        return;
    }
    if (code == KEY_MIYOO_L1) {
        if (p && menu_held) toggle_zoom();
        return;
    }
    if (!zoom_active || !p) return;

    if (code == KEY_DPAD_UP)    { zoom_y = clamp(zoom_y + PAN_STEP, 0, SCREEN_H-ZOOM_H); draw_zoom(); }
    if (code == KEY_DPAD_DOWN)  { zoom_y = clamp(zoom_y - PAN_STEP, 0, SCREEN_H-ZOOM_H); draw_zoom(); }
    if (code == KEY_DPAD_LEFT)  { zoom_x = clamp(zoom_x + PAN_STEP, 0, SCREEN_W-ZOOM_W); draw_zoom(); }
    if (code == KEY_DPAD_RIGHT) { zoom_x = clamp(zoom_x - PAN_STEP, 0, SCREEN_W-ZOOM_W); draw_zoom(); }
}

static volatile int running = 1;
static void sig_handler(int s) { (void)s; running = 0; }

int main(void) {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    int input_fd = open(INPUT_DEV, O_RDONLY | O_NONBLOCK);
    if (input_fd < 0) { perror("open input"); return 1; }
    if (fb_init() != 0) { close(input_fd); return 1; }

    printf("zoom_daemon v7: ready. Press MENU+L1 to toggle zoom.\n");
    fflush(stdout);

    struct pollfd pfd = { .fd = input_fd, .events = POLLIN };
    struct input_event ev;

    while (running) {
        if (poll(&pfd, 1, 100) <= 0) continue;
        if (pfd.revents & POLLIN) {
            ssize_t n = read(input_fd, &ev, sizeof(ev));
            if (n == (ssize_t)sizeof(ev) && ev.type == EV_KEY)
                handle_key(ev.code, ev.value);
        }
    }

    if (zoom_active) exit_zoom();
    close(input_fd);
    return 0;
}
