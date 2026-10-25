#ifndef KHOME_STATE_H
#define KHOME_STATE_H

/* The launcher's whole state: it draws the framebuffer and holds the
 * touchscreen (HOME), or has handed both to another app and is waiting for it
 * to exit. */
typedef enum {
    KHOME_STATE_HOME = 0,
    KHOME_STATE_BROWSER,
    KHOME_STATE_SHEET,
    KHOME_STATE_EDIT,
    KHOME_STATE_KOREADER,
    KHOME_STATE_NEWS,
    KHOME_STATE_NEWS_MENU,
    KHOME_STATE_SETTINGS,
    KHOME_STATE_SETTINGS_CONFIRM,
    KHOME_STATE_HOME_CONFIRM
} KhomeState;

/* Which confirm dialog is up.  Only meaningful while the state is
 * KHOME_STATE_SETTINGS_CONFIRM or KHOME_STATE_HOME_CONFIRM; every other state
 * carries KHOME_CONFIRM_NONE. */
typedef enum {
    KHOME_CONFIRM_NONE = 0,
    KHOME_CONFIRM_REBOOT,
    KHOME_CONFIRM_STOCK,
    KHOME_CONFIRM_REMOVE_FAVORITE
} KhomeConfirm;

/* What the URL editor does on Open: rewrite the active tab, or add the typed
 * URL as a new tab.  Held next to the launcher state, not inferred from the
 * buffer, so an empty New tab editor still means "new tab". */
typedef enum {
    KHOME_EDIT_CURRENT = 0,
    KHOME_EDIT_NEW
} KhomeEditMode;

typedef enum {
    KHOME_EV_TAP_BROWSER = 0,
    KHOME_EV_TAP_ADD,
    KHOME_EV_TAP_FAVORITE,
    KHOME_EV_TAP_MANGA,
    KHOME_EV_TAP_NEWSPAPER,
    KHOME_EV_TAP_SETTINGS,
    KHOME_EV_PAGE_PREV,
    KHOME_EV_PAGE_NEXT,
    KHOME_EV_POWER_GOING_TO_SS,
    KHOME_EV_POWER_OUT_OF_SS,
    KHOME_EV_BROWSER_EXIT,
    KHOME_EV_BROWSER_CLOSE,
    KHOME_EV_KOREADER_EXIT,
    KHOME_EV_DEFER_REDRAW,
    KHOME_EV_MINUTE,
    KHOME_EV_STATUS_CHANGED,
    KHOME_EV_SHEET_PULL,
    KHOME_EV_SHEET_BACK,
    KHOME_EV_SHEET_HOME,
    KHOME_EV_SHEET_NEW,
    KHOME_EV_SHEET_TAB,
    KHOME_EV_SHEET_CLOSE,
    KHOME_EV_SHEET_CLOSE_ACTIVE,
    KHOME_EV_SHEET_CLOSE_LAST,
    KHOME_EV_SHEET_DISMISS,
    KHOME_EV_SHEET_EDIT,
    KHOME_EV_SHEET_FAVORITE,
    KHOME_EV_SHEET_RELOAD,
    KHOME_EV_EDIT_CANCEL,
    KHOME_EV_EDIT_OPEN,
    KHOME_EV_DAY_CHANGED,
    KHOME_EV_SETTINGS_BACK,
    KHOME_EV_SETTINGS_REBOOT,
    KHOME_EV_SETTINGS_STOCK,
    KHOME_EV_SETTINGS_WIFI,
    KHOME_EV_SETTINGS_CHARGE_LIMIT,
    KHOME_EV_SETTINGS_BRIGHTNESS_DOWN,
    KHOME_EV_SETTINGS_BRIGHTNESS_UP,
    KHOME_EV_SETTINGS_UPDATE,
    KHOME_EV_NEWS_PREV,
    KHOME_EV_NEWS_NEXT,
    KHOME_EV_NEWS_HOME,
    KHOME_EV_NEWS_FETCHED,
    KHOME_EV_NEWS_MENU_HOME,
    KHOME_EV_NEWS_MENU_FRONT,
    KHOME_EV_NEWS_MENU_REFRESH,
    KHOME_EV_NEWS_MENU_BACK,
    KHOME_EV_CONFIRM_CANCEL,
    KHOME_EV_CONFIRM_REBOOT_KINDLE,
    KHOME_EV_CONFIRM_REBOOT_HOME,
    KHOME_EV_CONFIRM_STOCK_BACK,
    KHOME_EV_QUIT,
    KHOME_EV_START,
    KHOME_EV_LONG_FAVORITE,
    KHOME_EV_CONFIRM_REMOVE_FAVORITE
} KhomeEvent;

typedef enum {
    KHOME_ACT_GRAB = 0,
    KHOME_ACT_UNGRAB,
    KHOME_ACT_DRAW_HOME,
    KHOME_ACT_DRAW_CLOCK,
    KHOME_ACT_PAGE_PREV,
    KHOME_ACT_PAGE_NEXT,
    KHOME_ACT_START_BROWSER,
    KHOME_ACT_FAVORITE_OPEN,
    KHOME_ACT_CLOSE_BROWSER,
    KHOME_ACT_START_KOREADER,
    KHOME_ACT_SCHEDULE_REDRAW,
    KHOME_ACT_DRAW_SHEET,
    KHOME_ACT_HIDE_SHEET,
    KHOME_ACT_DRAW_EDIT,
    KHOME_ACT_HIDE_EDIT,
    KHOME_ACT_EDIT_OPEN,
    KHOME_ACT_OPEN_URL,
    KHOME_ACT_BROWSER_BACK,
    KHOME_ACT_SHEET_TAB,
    KHOME_ACT_SHEET_FAVORITE,
    KHOME_ACT_SHEET_CLOSE,
    KHOME_ACT_SHEET_CLOSE_ACTIVE,
    KHOME_ACT_SHEET_CLOSE_LAST,
    KHOME_ACT_SHEET_RELOAD,
    KHOME_ACT_DRAW_SHEET_STATUS,
    KHOME_ACT_DRAW_SETTINGS,
    KHOME_ACT_DRAW_CONFIRM,
    KHOME_ACT_DRAW_NEWS,
    KHOME_ACT_DRAW_NEWS_STATUS,
    KHOME_ACT_DRAW_NEWS_MENU,
    KHOME_ACT_DRAW_NEWS_MENU_STATUS,
    KHOME_ACT_NEWS_MENU_REFRESH,
    KHOME_ACT_NEWS_PREV,
    KHOME_ACT_NEWS_NEXT,
    KHOME_ACT_NEWS_FRONT,
    KHOME_ACT_NEWS_BACK,
    KHOME_ACT_NEWS_FETCH,
    KHOME_ACT_WIFI_TOGGLE,
    KHOME_ACT_CHARGE_LIMIT_TOGGLE,
    KHOME_ACT_BRIGHTNESS_DOWN,
    KHOME_ACT_BRIGHTNESS_UP,
    KHOME_ACT_QUIT_REBOOT_HOME,
    KHOME_ACT_QUIT_REBOOT_KINDLE,
    KHOME_ACT_QUIT,
    KHOME_ACT_REMOVE_FAVORITE
} KhomeAction;

/* khome.sh watches for these exit codes.  Keep the numbers in sync with
 * kindle/khome/khome.sh. */
#define KHOME_EXIT_REBOOT_HOME 75
#define KHOME_EXIT_REBOOT_KINDLE 76

#define KHOME_MAX_ACTIONS 4

/* Sleep is orthogonal to which app owns the screen, so it rides alongside the
 * state instead of becoming a fifth state. */
typedef struct {
    KhomeState state;
    int asleep;
    KhomeConfirm confirm;
} KhomeMachine;

typedef struct {
    KhomeState state;
    int asleep;
    KhomeConfirm confirm;
    KhomeAction actions[KHOME_MAX_ACTIONS];
    int action_count;
} KhomeStep;

/* Pure: machine + event -> next machine + the side effects main must run.  The
 * tab index for the sheet_tab/sheet_close events lives in main. */
KhomeStep khome_step(KhomeMachine machine, KhomeEvent ev);

const char *khome_state_name(KhomeState state);
const char *khome_event_name(KhomeEvent ev);
const char *khome_action_name(KhomeAction act);

#endif
