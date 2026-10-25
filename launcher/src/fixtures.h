#ifndef KHOME_FIXTURES_H
#define KHOME_FIXTURES_H

#include "model.h"
#include "urledit.h"

/* Display data copied from the mock HTML files under design/system/src. */
void fixture_home(HomeModel *m);
void fixture_home_many(HomeModel *m);
void fixture_home_many_page(HomeModel *m, int page);
void fixture_home_empty(HomeModel *m);
void fixture_settings(SettingsModel *m, int wifi_on);
void fixture_settings_updating(SettingsModel *m);
void fixture_launching(LaunchingModel *m);
void fixture_sleep(SleepModel *m);
/* NEWS before the first fetch: no pages on disk and Wi-Fi off. */
void fixture_news_empty(HomeModel *m);
/* Tab sheet samples: three tabs with the second active, and a full eight. */
void fixture_tab_sheet_3(TabSheetModel *m);
void fixture_tab_sheet_8(TabSheetModel *m);
/* URL editor samples: lowercase keys with the cursor mid-URL, and the symbol
 * layout with the cursor in the fifth wrapped line. */
void fixture_url_edit(UrlEdit *edit);
void fixture_url_edit_symbols(UrlEdit *edit);

#endif
