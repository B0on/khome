#ifndef KHOME_MODEL_H
#define KHOME_MODEL_H

#include "tabs.h"

/* Data shared by the home, settings, launching and sleep screens. */
typedef struct {
    int hour;
    int minute;
    int day_of_year;
    int days_in_year;
    char date[48];    /* "Oct 23" */
    char weekday[24]; /* "Fri" */
    char ssid[128];
    int wifi_on;
    int battery_level; /* 0..100, or -1 when unknown */
    /* The browser's settings.orientation, "landscapeLeft" or portrait. */
    int browser_landscape;
    Favorite favorites[KHOME_MAX_FAVORITES];
    int favorite_count;
    int page; /* 0-based, only used when favorite_count > 6 */
    char notice[96]; /* one line in the footer after a failed browser launch */
} HomeModel;

typedef struct {
    HomeModel home;
    int brightness; /* 0..16 */
    int charge_limit; /* 1 on, 0 off, -1 when cv_prm is unreadable */
    char ip[32]; /* wlan0 IPv4, empty when unknown */
    char last_update[48]; /* "Oct 23 06:00" */
    char version[64];
    int updating; /* a fetch child is running */
    char notice[96]; /* one line under Newspaper, then cleared */
} SettingsModel;

typedef struct {
    HomeModel home;
    char app_name[48];
    int app_icon; /* one of the ICON_* constants */
} LaunchingModel;

typedef struct {
    int day_of_year;
    int days_in_year;
    char date[48];
    char weekday[24];
} SleepModel;

/* Data for the tab list sheet that overlays the browser. */
typedef struct {
    TabList tabs;
    int hour;
    int minute;
    int battery_level; /* 0..100, or -1 when unknown */
    int favorite; /* 1 when the active tab's URL is in favorites.tsv */
    char notice[96]; /* one line for the next sheet draw, then cleared */
} TabSheetModel;

/* What the NEWS pull-down menu shows.  article selects the label and the
 * Back to paper button; position and count are 1-based and 0 when empty. */
typedef struct {
    int hour;
    int minute;
    int battery_level;
    int article;
    int position;
    int count;
    char notice[96]; /* one status line in place of the position, then cleared */
} NewsMenuModel;

#endif
