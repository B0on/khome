#include "state.h"

#include <stddef.h>

static KhomeStep step(KhomeMachine machine)
{
    KhomeStep s;
    s.state = machine.state;
    s.asleep = machine.asleep;
    s.confirm = machine.confirm;
    s.action_count = 0;
    return s;
}

static void add(KhomeStep *s, KhomeAction act)
{
    if (s->action_count < KHOME_MAX_ACTIONS) {
        s->actions[s->action_count++] = act;
    }
}

KhomeStep khome_step(KhomeMachine machine, KhomeEvent ev)
{
    KhomeStep s = step(machine);

    if (ev == KHOME_EV_POWER_GOING_TO_SS) {
        s.asleep = 1;
        if (s.state == KHOME_STATE_SHEET || s.state == KHOME_STATE_EDIT) {
            /* Going under the saver hides the sheet or editor, so hand the
             * panel back to the browser the same way outOfScreenSaver does. */
            KhomeState was = s.state;

            s.state = KHOME_STATE_BROWSER;
            add(&s, KHOME_ACT_UNGRAB);
            if (was == KHOME_STATE_EDIT) {
                add(&s, KHOME_ACT_HIDE_EDIT);
            } else {
                add(&s, KHOME_ACT_HIDE_SHEET);
            }
        } else if (s.state == KHOME_STATE_NEWS_MENU) {
            /* The menu has no shield or band; dropping back to NEWS lets the
             * wake redraw paint the page under it. */
            s.state = KHOME_STATE_NEWS;
        }
        return s;
    }
    if (ev == KHOME_EV_POWER_OUT_OF_SS) {
        s.asleep = 0;
        /* The stock screen saver leaves our framebuffer intact, but the
         * panel keeps the saver image until something rewrites it. */
        if (s.state == KHOME_STATE_HOME || s.state == KHOME_STATE_NEWS ||
            s.state == KHOME_STATE_NEWS_MENU ||
            s.state == KHOME_STATE_SETTINGS ||
            s.state == KHOME_STATE_SETTINGS_CONFIRM ||
            s.state == KHOME_STATE_HOME_CONFIRM) {
            add(&s, KHOME_ACT_SCHEDULE_REDRAW);
        } else if (s.state == KHOME_STATE_SHEET ||
                   s.state == KHOME_STATE_EDIT) {
            /* A spurious wake while the sheet or editor is up leaves it on
             * screen after the state has handed the panel back to the
             * browser, so hide it here just as the other exit paths do. */
            KhomeState was = s.state;

            s.state = KHOME_STATE_BROWSER;
            add(&s, KHOME_ACT_UNGRAB);
            if (was == KHOME_STATE_EDIT) {
                add(&s, KHOME_ACT_HIDE_EDIT);
            } else {
                add(&s, KHOME_ACT_HIDE_SHEET);
            }
        }
        return s;
    }
    if (s.asleep && ev != KHOME_EV_QUIT) {
        /* The saver owns the panel, so nothing here may draw.  An app that
         * died is still dead, though, and the wake redraw catches up. */
        switch (ev) {
        case KHOME_EV_BROWSER_EXIT:
            if (s.state == KHOME_STATE_BROWSER ||
                s.state == KHOME_STATE_SHEET ||
                s.state == KHOME_STATE_EDIT) {
                s.state = KHOME_STATE_HOME;
                add(&s, KHOME_ACT_GRAB);
            }
            break;
        case KHOME_EV_KOREADER_EXIT:
            if (s.state == KHOME_STATE_KOREADER) {
                s.state = KHOME_STATE_HOME;
                add(&s, KHOME_ACT_GRAB);
            }
            break;
        case KHOME_EV_BROWSER_CLOSE:
            if (s.state == KHOME_STATE_BROWSER ||
                s.state == KHOME_STATE_SHEET ||
                s.state == KHOME_STATE_EDIT) {
                s.state = KHOME_STATE_HOME;
                add(&s, KHOME_ACT_CLOSE_BROWSER);
            }
            break;
        default:
            break;
        }
        return s;
    }

    switch (ev) {
    case KHOME_EV_TAP_BROWSER:
    case KHOME_EV_TAP_ADD:
        if (s.state == KHOME_STATE_HOME) {
            s.state = KHOME_STATE_BROWSER;
            add(&s, KHOME_ACT_UNGRAB);
            add(&s, KHOME_ACT_START_BROWSER);
        }
        break;
    case KHOME_EV_TAP_MANGA:
        if (s.state == KHOME_STATE_HOME) {
            s.state = KHOME_STATE_KOREADER;
            add(&s, KHOME_ACT_UNGRAB);
            add(&s, KHOME_ACT_START_KOREADER);
        }
        break;
    case KHOME_EV_TAP_FAVORITE:
        if (s.state == KHOME_STATE_HOME) {
            s.state = KHOME_STATE_BROWSER;
            add(&s, KHOME_ACT_UNGRAB);
            add(&s, KHOME_ACT_FAVORITE_OPEN);
        }
        break;
    case KHOME_EV_LONG_FAVORITE:
        if (s.state == KHOME_STATE_HOME) {
            s.state = KHOME_STATE_HOME_CONFIRM;
            s.confirm = KHOME_CONFIRM_REMOVE_FAVORITE;
            add(&s, KHOME_ACT_DRAW_CONFIRM);
        }
        break;
    case KHOME_EV_TAP_NEWSPAPER:
        if (s.state == KHOME_STATE_HOME) {
            s.state = KHOME_STATE_NEWS;
            add(&s, KHOME_ACT_DRAW_NEWS);
            add(&s, KHOME_ACT_NEWS_FETCH);
        }
        break;
    case KHOME_EV_PAGE_PREV:
        if (s.state == KHOME_STATE_HOME) {
            add(&s, KHOME_ACT_PAGE_PREV);
        } else if (s.state == KHOME_STATE_NEWS) {
            add(&s, KHOME_ACT_NEWS_PREV);
        }
        break;
    case KHOME_EV_PAGE_NEXT:
        if (s.state == KHOME_STATE_HOME) {
            add(&s, KHOME_ACT_PAGE_NEXT);
        } else if (s.state == KHOME_STATE_NEWS) {
            add(&s, KHOME_ACT_NEWS_NEXT);
        }
        break;
    case KHOME_EV_NEWS_PREV:
        if (s.state == KHOME_STATE_NEWS) {
            add(&s, KHOME_ACT_NEWS_PREV);
        }
        break;
    case KHOME_EV_NEWS_NEXT:
        if (s.state == KHOME_STATE_NEWS) {
            add(&s, KHOME_ACT_NEWS_NEXT);
        }
        break;
    case KHOME_EV_NEWS_HOME:
        if (s.state == KHOME_STATE_NEWS) {
            s.state = KHOME_STATE_HOME;
            add(&s, KHOME_ACT_DRAW_HOME);
        }
        break;
    case KHOME_EV_NEWS_FETCHED:
        if (s.state == KHOME_STATE_NEWS) {
            add(&s, KHOME_ACT_DRAW_NEWS);
        } else if (s.state == KHOME_STATE_NEWS_MENU) {
            add(&s, KHOME_ACT_DRAW_NEWS_MENU);
        } else if (s.state == KHOME_STATE_SETTINGS) {
            add(&s, KHOME_ACT_DRAW_SETTINGS);
        }
        break;
    case KHOME_EV_TAP_SETTINGS:
        if (s.state == KHOME_STATE_HOME) {
            s.state = KHOME_STATE_SETTINGS;
            add(&s, KHOME_ACT_DRAW_SETTINGS);
        }
        break;
    case KHOME_EV_SETTINGS_BACK:
        if (s.state == KHOME_STATE_SETTINGS) {
            s.state = KHOME_STATE_HOME;
            add(&s, KHOME_ACT_DRAW_HOME);
        }
        break;
    case KHOME_EV_SETTINGS_REBOOT:
        if (s.state == KHOME_STATE_SETTINGS) {
            s.state = KHOME_STATE_SETTINGS_CONFIRM;
            s.confirm = KHOME_CONFIRM_REBOOT;
            add(&s, KHOME_ACT_DRAW_CONFIRM);
        }
        break;
    case KHOME_EV_SETTINGS_STOCK:
        if (s.state == KHOME_STATE_SETTINGS) {
            s.state = KHOME_STATE_SETTINGS_CONFIRM;
            s.confirm = KHOME_CONFIRM_STOCK;
            add(&s, KHOME_ACT_DRAW_CONFIRM);
        }
        break;
    case KHOME_EV_SETTINGS_WIFI:
        if (s.state == KHOME_STATE_SETTINGS) {
            add(&s, KHOME_ACT_WIFI_TOGGLE);
        }
        break;
    case KHOME_EV_SETTINGS_CHARGE_LIMIT:
        if (s.state == KHOME_STATE_SETTINGS) {
            add(&s, KHOME_ACT_CHARGE_LIMIT_TOGGLE);
        }
        break;
    case KHOME_EV_SETTINGS_BRIGHTNESS_DOWN:
        if (s.state == KHOME_STATE_SETTINGS) {
            add(&s, KHOME_ACT_BRIGHTNESS_DOWN);
        }
        break;
    case KHOME_EV_SETTINGS_BRIGHTNESS_UP:
        if (s.state == KHOME_STATE_SETTINGS) {
            add(&s, KHOME_ACT_BRIGHTNESS_UP);
        }
        break;
    case KHOME_EV_SETTINGS_UPDATE:
        if (s.state == KHOME_STATE_SETTINGS) {
            add(&s, KHOME_ACT_NEWS_FETCH);
        }
        break;
    case KHOME_EV_CONFIRM_CANCEL:
        if (s.state == KHOME_STATE_SETTINGS_CONFIRM) {
            s.state = KHOME_STATE_SETTINGS;
            s.confirm = KHOME_CONFIRM_NONE;
            add(&s, KHOME_ACT_DRAW_SETTINGS);
        } else if (s.state == KHOME_STATE_HOME_CONFIRM) {
            s.state = KHOME_STATE_HOME;
            s.confirm = KHOME_CONFIRM_NONE;
            add(&s, KHOME_ACT_DRAW_HOME);
        }
        break;
    case KHOME_EV_CONFIRM_REMOVE_FAVORITE:
        if (s.state == KHOME_STATE_HOME_CONFIRM &&
            s.confirm == KHOME_CONFIRM_REMOVE_FAVORITE) {
            s.state = KHOME_STATE_HOME;
            s.confirm = KHOME_CONFIRM_NONE;
            add(&s, KHOME_ACT_REMOVE_FAVORITE);
            add(&s, KHOME_ACT_DRAW_HOME);
        }
        break;
    case KHOME_EV_CONFIRM_REBOOT_KINDLE:
        if (s.state == KHOME_STATE_SETTINGS_CONFIRM &&
            s.confirm == KHOME_CONFIRM_REBOOT) {
            add(&s, KHOME_ACT_QUIT_REBOOT_KINDLE);
        }
        break;
    case KHOME_EV_CONFIRM_REBOOT_HOME:
        if (s.state == KHOME_STATE_SETTINGS_CONFIRM &&
            s.confirm == KHOME_CONFIRM_REBOOT) {
            add(&s, KHOME_ACT_QUIT_REBOOT_HOME);
        }
        break;
    case KHOME_EV_CONFIRM_STOCK_BACK:
        if (s.state == KHOME_STATE_SETTINGS_CONFIRM &&
            s.confirm == KHOME_CONFIRM_STOCK) {
            add(&s, KHOME_ACT_QUIT);
        }
        break;
    case KHOME_EV_BROWSER_EXIT:
        if (s.state == KHOME_STATE_BROWSER || s.state == KHOME_STATE_SHEET ||
            s.state == KHOME_STATE_EDIT) {
            s.state = KHOME_STATE_HOME;
            add(&s, KHOME_ACT_GRAB);
            add(&s, KHOME_ACT_DRAW_HOME);
        }
        break;
    case KHOME_EV_BROWSER_CLOSE:
        if (s.state == KHOME_STATE_BROWSER || s.state == KHOME_STATE_SHEET ||
            s.state == KHOME_STATE_EDIT) {
            s.state = KHOME_STATE_HOME;
            add(&s, KHOME_ACT_CLOSE_BROWSER);
        }
        break;
    case KHOME_EV_SHEET_PULL:
        if (s.state == KHOME_STATE_BROWSER) {
            s.state = KHOME_STATE_SHEET;
            add(&s, KHOME_ACT_GRAB);
            add(&s, KHOME_ACT_DRAW_SHEET);
        } else if (s.state == KHOME_STATE_NEWS) {
            /* NEWS already owns the touchscreen, and khome draws the page
             * itself, so the menu needs neither grab nor shield. */
            s.state = KHOME_STATE_NEWS_MENU;
            add(&s, KHOME_ACT_DRAW_NEWS_MENU);
        }
        break;
    case KHOME_EV_SHEET_DISMISS:
        if (s.state == KHOME_STATE_SHEET) {
            s.state = KHOME_STATE_BROWSER;
            add(&s, KHOME_ACT_UNGRAB);
            add(&s, KHOME_ACT_HIDE_SHEET);
        } else if (s.state == KHOME_STATE_NEWS_MENU) {
            s.state = KHOME_STATE_NEWS;
            add(&s, KHOME_ACT_DRAW_NEWS);
        }
        break;
    case KHOME_EV_NEWS_MENU_HOME:
        if (s.state == KHOME_STATE_NEWS_MENU) {
            s.state = KHOME_STATE_HOME;
            add(&s, KHOME_ACT_DRAW_HOME);
        }
        break;
    case KHOME_EV_NEWS_MENU_FRONT:
        if (s.state == KHOME_STATE_NEWS_MENU) {
            s.state = KHOME_STATE_NEWS;
            add(&s, KHOME_ACT_NEWS_FRONT);
        }
        break;
    case KHOME_EV_NEWS_MENU_REFRESH:
        if (s.state == KHOME_STATE_NEWS_MENU) {
            /* main closes the menu only once the fetch really starts, so a
             * Wi-Fi or script failure can leave its notice on the panel. */
            add(&s, KHOME_ACT_NEWS_MENU_REFRESH);
        }
        break;
    case KHOME_EV_NEWS_MENU_BACK:
        if (s.state == KHOME_STATE_NEWS_MENU) {
            s.state = KHOME_STATE_NEWS;
            add(&s, KHOME_ACT_NEWS_BACK);
        }
        break;
    case KHOME_EV_SHEET_EDIT:
        if (s.state == KHOME_STATE_SHEET) {
            s.state = KHOME_STATE_EDIT;
            add(&s, KHOME_ACT_DRAW_EDIT);
        }
        break;
    case KHOME_EV_EDIT_CANCEL:
        if (s.state == KHOME_STATE_EDIT) {
            s.state = KHOME_STATE_SHEET;
            add(&s, KHOME_ACT_HIDE_EDIT);
            add(&s, KHOME_ACT_DRAW_SHEET);
        }
        break;
    case KHOME_EV_EDIT_OPEN:
        if (s.state == KHOME_STATE_EDIT) {
            s.state = KHOME_STATE_BROWSER;
            add(&s, KHOME_ACT_EDIT_OPEN);
            add(&s, KHOME_ACT_UNGRAB);
            add(&s, KHOME_ACT_HIDE_EDIT);
            add(&s, KHOME_ACT_OPEN_URL);
        }
        break;
    case KHOME_EV_SHEET_BACK:
        if (s.state == KHOME_STATE_SHEET) {
            s.state = KHOME_STATE_BROWSER;
            add(&s, KHOME_ACT_BROWSER_BACK);
            add(&s, KHOME_ACT_UNGRAB);
            add(&s, KHOME_ACT_HIDE_SHEET);
        }
        break;
    case KHOME_EV_SHEET_HOME:
        if (s.state == KHOME_STATE_SHEET) {
            s.state = KHOME_STATE_HOME;
            add(&s, KHOME_ACT_CLOSE_BROWSER);
        }
        break;
    case KHOME_EV_SHEET_NEW:
        if (s.state == KHOME_STATE_SHEET) {
            s.state = KHOME_STATE_EDIT;
            add(&s, KHOME_ACT_DRAW_EDIT);
        }
        break;
    case KHOME_EV_SHEET_TAB:
        if (s.state == KHOME_STATE_SHEET) {
            s.state = KHOME_STATE_BROWSER;
            add(&s, KHOME_ACT_SHEET_TAB);
            add(&s, KHOME_ACT_UNGRAB);
            add(&s, KHOME_ACT_HIDE_SHEET);
        }
        break;
    case KHOME_EV_SHEET_FAVORITE:
        if (s.state == KHOME_STATE_SHEET) {
            add(&s, KHOME_ACT_SHEET_FAVORITE);
        }
        break;
    case KHOME_EV_SHEET_RELOAD:
        if (s.state == KHOME_STATE_SHEET) {
            s.state = KHOME_STATE_BROWSER;
            add(&s, KHOME_ACT_SHEET_RELOAD);
            add(&s, KHOME_ACT_UNGRAB);
            add(&s, KHOME_ACT_HIDE_SHEET);
        }
        break;
    case KHOME_EV_SHEET_CLOSE:
        if (s.state == KHOME_STATE_SHEET) {
            add(&s, KHOME_ACT_SHEET_CLOSE);
        }
        break;
    case KHOME_EV_SHEET_CLOSE_ACTIVE:
        if (s.state == KHOME_STATE_SHEET) {
            s.state = KHOME_STATE_BROWSER;
            add(&s, KHOME_ACT_SHEET_CLOSE_ACTIVE);
            add(&s, KHOME_ACT_UNGRAB);
            add(&s, KHOME_ACT_HIDE_SHEET);
        }
        break;
    case KHOME_EV_SHEET_CLOSE_LAST:
        if (s.state == KHOME_STATE_SHEET) {
            s.state = KHOME_STATE_HOME;
            add(&s, KHOME_ACT_SHEET_CLOSE_LAST);
        }
        break;
    case KHOME_EV_KOREADER_EXIT:
        if (s.state == KHOME_STATE_KOREADER) {
            s.state = KHOME_STATE_HOME;
            add(&s, KHOME_ACT_GRAB);
            add(&s, KHOME_ACT_DRAW_HOME);
        }
        break;
    case KHOME_EV_DEFER_REDRAW:
        if (s.state == KHOME_STATE_HOME) {
            add(&s, KHOME_ACT_DRAW_HOME);
        } else if (s.state == KHOME_STATE_NEWS) {
            add(&s, KHOME_ACT_DRAW_NEWS);
        } else if (s.state == KHOME_STATE_NEWS_MENU) {
            add(&s, KHOME_ACT_DRAW_NEWS_MENU);
        } else if (s.state == KHOME_STATE_SETTINGS) {
            add(&s, KHOME_ACT_DRAW_SETTINGS);
        } else if (s.state == KHOME_STATE_SETTINGS_CONFIRM ||
                   s.state == KHOME_STATE_HOME_CONFIRM) {
            add(&s, KHOME_ACT_DRAW_CONFIRM);
        }
        break;
    case KHOME_EV_MINUTE:
        if (s.state == KHOME_STATE_HOME) {
            add(&s, KHOME_ACT_DRAW_CLOCK);
        } else if (s.state == KHOME_STATE_SHEET) {
            add(&s, KHOME_ACT_DRAW_SHEET_STATUS);
        } else if (s.state == KHOME_STATE_NEWS_MENU) {
            add(&s, KHOME_ACT_DRAW_NEWS_MENU_STATUS);
        }
        break;
    case KHOME_EV_STATUS_CHANGED:
        if (s.state == KHOME_STATE_HOME) {
            add(&s, KHOME_ACT_DRAW_HOME);
        } else if (s.state == KHOME_STATE_NEWS) {
            add(&s, KHOME_ACT_DRAW_NEWS_STATUS);
        } else if (s.state == KHOME_STATE_NEWS_MENU) {
            add(&s, KHOME_ACT_DRAW_NEWS_MENU_STATUS);
        } else if (s.state == KHOME_STATE_SHEET) {
            add(&s, KHOME_ACT_DRAW_SHEET_STATUS);
        } else if (s.state == KHOME_STATE_SETTINGS) {
            add(&s, KHOME_ACT_DRAW_SETTINGS);
        } else if (s.state == KHOME_STATE_SETTINGS_CONFIRM ||
                   s.state == KHOME_STATE_HOME_CONFIRM) {
            add(&s, KHOME_ACT_DRAW_CONFIRM);
        }
        break;
    case KHOME_EV_DAY_CHANGED:
        /* The masthead date and NO. sit outside the clock rectangle. */
        if (s.state == KHOME_STATE_HOME) {
            add(&s, KHOME_ACT_DRAW_HOME);
        } else if (s.state == KHOME_STATE_NEWS) {
            add(&s, KHOME_ACT_DRAW_NEWS_STATUS);
        } else if (s.state == KHOME_STATE_SETTINGS) {
            add(&s, KHOME_ACT_DRAW_SETTINGS);
        } else if (s.state == KHOME_STATE_SETTINGS_CONFIRM ||
                   s.state == KHOME_STATE_HOME_CONFIRM) {
            add(&s, KHOME_ACT_DRAW_CONFIRM);
        }
        break;
    case KHOME_EV_QUIT:
        add(&s, KHOME_ACT_QUIT);
        break;
    case KHOME_EV_START:
        if (s.state == KHOME_STATE_HOME) {
            add(&s, KHOME_ACT_GRAB);
            add(&s, KHOME_ACT_DRAW_HOME);
        }
        break;
    default:
        break;
    }
    return s;
}

const char *khome_state_name(KhomeState state)
{
    switch (state) {
    case KHOME_STATE_HOME:
        return "HOME";
    case KHOME_STATE_BROWSER:
        return "BROWSER";
    case KHOME_STATE_SHEET:
        return "SHEET";
    case KHOME_STATE_EDIT:
        return "EDIT";
    case KHOME_STATE_KOREADER:
        return "KOREADER";
    case KHOME_STATE_NEWS:
        return "NEWS";
    case KHOME_STATE_NEWS_MENU:
        return "NEWS_MENU";
    case KHOME_STATE_SETTINGS:
        return "SETTINGS";
    case KHOME_STATE_SETTINGS_CONFIRM:
        return "SETTINGS_CONFIRM";
    case KHOME_STATE_HOME_CONFIRM:
        return "HOME_CONFIRM";
    }
    return "?";
}

const char *khome_event_name(KhomeEvent ev)
{
    switch (ev) {
    case KHOME_EV_TAP_BROWSER:
        return "tap-browser";
    case KHOME_EV_TAP_ADD:
        return "tap-add";
    case KHOME_EV_TAP_FAVORITE:
        return "tap-favorite";
    case KHOME_EV_TAP_MANGA:
        return "tap-manga";
    case KHOME_EV_TAP_NEWSPAPER:
        return "tap-newspaper";
    case KHOME_EV_TAP_SETTINGS:
        return "tap-settings";
    case KHOME_EV_PAGE_PREV:
        return "page-prev";
    case KHOME_EV_PAGE_NEXT:
        return "page-next";
    case KHOME_EV_POWER_GOING_TO_SS:
        return "goingToScreenSaver";
    case KHOME_EV_POWER_OUT_OF_SS:
        return "outOfScreenSaver";
    case KHOME_EV_BROWSER_EXIT:
        return "browser-exit";
    case KHOME_EV_BROWSER_CLOSE:
        return "browser-close";
    case KHOME_EV_KOREADER_EXIT:
        return "koreader-exit";
    case KHOME_EV_DEFER_REDRAW:
        return "defer-redraw";
    case KHOME_EV_MINUTE:
        return "minute";
    case KHOME_EV_STATUS_CHANGED:
        return "status-changed";
    case KHOME_EV_SHEET_PULL:
        return "sheet-pull";
    case KHOME_EV_SHEET_BACK:
        return "sheet-back";
    case KHOME_EV_SHEET_HOME:
        return "sheet-home";
    case KHOME_EV_SHEET_NEW:
        return "sheet-new";
    case KHOME_EV_SHEET_TAB:
        return "sheet-tab";
    case KHOME_EV_SHEET_CLOSE:
        return "sheet-close";
    case KHOME_EV_SHEET_CLOSE_ACTIVE:
        return "sheet-close-active";
    case KHOME_EV_SHEET_CLOSE_LAST:
        return "sheet-close-last";
    case KHOME_EV_SHEET_DISMISS:
        return "sheet-dismiss";
    case KHOME_EV_SHEET_EDIT:
        return "sheet-edit";
    case KHOME_EV_SHEET_FAVORITE:
        return "sheet-favorite";
    case KHOME_EV_SHEET_RELOAD:
        return "sheet-reload";
    case KHOME_EV_EDIT_CANCEL:
        return "edit-cancel";
    case KHOME_EV_EDIT_OPEN:
        return "edit-open";
    case KHOME_EV_DAY_CHANGED:
        return "day-changed";
    case KHOME_EV_SETTINGS_BACK:
        return "settings-back";
    case KHOME_EV_SETTINGS_REBOOT:
        return "settings-reboot";
    case KHOME_EV_SETTINGS_STOCK:
        return "settings-stock";
    case KHOME_EV_SETTINGS_WIFI:
        return "settings-wifi";
    case KHOME_EV_SETTINGS_CHARGE_LIMIT:
        return "settings-charge-limit";
    case KHOME_EV_SETTINGS_BRIGHTNESS_DOWN:
        return "settings-brightness-down";
    case KHOME_EV_SETTINGS_BRIGHTNESS_UP:
        return "settings-brightness-up";
    case KHOME_EV_SETTINGS_UPDATE:
        return "settings-update";
    case KHOME_EV_NEWS_PREV:
        return "news-prev";
    case KHOME_EV_NEWS_NEXT:
        return "news-next";
    case KHOME_EV_NEWS_HOME:
        return "news-home";
    case KHOME_EV_NEWS_FETCHED:
        return "news-fetched";
    case KHOME_EV_NEWS_MENU_HOME:
        return "news-menu-home";
    case KHOME_EV_NEWS_MENU_FRONT:
        return "news-menu-front";
    case KHOME_EV_NEWS_MENU_REFRESH:
        return "news-menu-refresh";
    case KHOME_EV_NEWS_MENU_BACK:
        return "news-menu-back";
    case KHOME_EV_CONFIRM_CANCEL:
        return "confirm-cancel";
    case KHOME_EV_CONFIRM_REBOOT_KINDLE:
        return "confirm-reboot-kindle";
    case KHOME_EV_CONFIRM_REBOOT_HOME:
        return "confirm-reboot-home";
    case KHOME_EV_CONFIRM_STOCK_BACK:
        return "confirm-stock-back";
    case KHOME_EV_QUIT:
        return "quit";
    case KHOME_EV_START:
        return "start";
    case KHOME_EV_LONG_FAVORITE:
        return "long-favorite";
    case KHOME_EV_CONFIRM_REMOVE_FAVORITE:
        return "confirm-remove-favorite";
    }
    return "?";
}

const char *khome_action_name(KhomeAction act)
{
    switch (act) {
    case KHOME_ACT_GRAB:
        return "grab";
    case KHOME_ACT_UNGRAB:
        return "ungrab";
    case KHOME_ACT_DRAW_HOME:
        return "draw-home";
    case KHOME_ACT_DRAW_CLOCK:
        return "draw-clock";
    case KHOME_ACT_PAGE_PREV:
        return "page-prev";
    case KHOME_ACT_PAGE_NEXT:
        return "page-next";
    case KHOME_ACT_START_BROWSER:
        return "start-browser";
    case KHOME_ACT_FAVORITE_OPEN:
        return "favorite-open";
    case KHOME_ACT_CLOSE_BROWSER:
        return "close-browser";
    case KHOME_ACT_START_KOREADER:
        return "start-koreader";
    case KHOME_ACT_SCHEDULE_REDRAW:
        return "schedule-redraw";
    case KHOME_ACT_DRAW_SHEET:
        return "draw-sheet";
    case KHOME_ACT_HIDE_SHEET:
        return "hide-sheet";
    case KHOME_ACT_DRAW_EDIT:
        return "draw-edit";
    case KHOME_ACT_HIDE_EDIT:
        return "hide-edit";
    case KHOME_ACT_EDIT_OPEN:
        return "edit-open";
    case KHOME_ACT_OPEN_URL:
        return "open-url";
    case KHOME_ACT_BROWSER_BACK:
        return "browser-back";
    case KHOME_ACT_SHEET_TAB:
        return "sheet-tab";
    case KHOME_ACT_SHEET_FAVORITE:
        return "sheet-favorite";
    case KHOME_ACT_SHEET_CLOSE:
        return "sheet-close";
    case KHOME_ACT_SHEET_CLOSE_ACTIVE:
        return "sheet-close-active";
    case KHOME_ACT_SHEET_CLOSE_LAST:
        return "sheet-close-last";
    case KHOME_ACT_SHEET_RELOAD:
        return "sheet-reload";
    case KHOME_ACT_DRAW_SHEET_STATUS:
        return "draw-sheet-status";
    case KHOME_ACT_DRAW_SETTINGS:
        return "draw-settings";
    case KHOME_ACT_DRAW_CONFIRM:
        return "draw-confirm";
    case KHOME_ACT_DRAW_NEWS:
        return "draw-news";
    case KHOME_ACT_DRAW_NEWS_STATUS:
        return "draw-news-status";
    case KHOME_ACT_DRAW_NEWS_MENU:
        return "draw-news-menu";
    case KHOME_ACT_DRAW_NEWS_MENU_STATUS:
        return "draw-news-menu-status";
    case KHOME_ACT_NEWS_MENU_REFRESH:
        return "news-menu-refresh";
    case KHOME_ACT_NEWS_PREV:
        return "news-prev";
    case KHOME_ACT_NEWS_NEXT:
        return "news-next";
    case KHOME_ACT_NEWS_FRONT:
        return "news-front";
    case KHOME_ACT_NEWS_BACK:
        return "news-back";
    case KHOME_ACT_NEWS_FETCH:
        return "news-fetch";
    case KHOME_ACT_WIFI_TOGGLE:
        return "wifi-toggle";
    case KHOME_ACT_CHARGE_LIMIT_TOGGLE:
        return "charge-limit-toggle";
    case KHOME_ACT_BRIGHTNESS_DOWN:
        return "brightness-down";
    case KHOME_ACT_BRIGHTNESS_UP:
        return "brightness-up";
    case KHOME_ACT_QUIT_REBOOT_HOME:
        return "quit-reboot-home";
    case KHOME_ACT_QUIT_REBOOT_KINDLE:
        return "quit-reboot-kindle";
    case KHOME_ACT_QUIT:
        return "quit";
    case KHOME_ACT_REMOVE_FAVORITE:
        return "remove-favorite";
    }
    return "?";
}
