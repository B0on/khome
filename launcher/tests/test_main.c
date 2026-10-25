#include "suites.h"
#include "test_harness.h"

void kh_run_favorites_tests(void);
void kh_run_gaps_tests(void);
void kh_run_news_tests(void);
void kh_run_browser_row_tests(void);
void kh_run_browser_orientation_tests(void);
void kh_run_long_press_tests(void);

int main(void)
{
    printf("khome unit tests\n");
    kh_run_text_tests();
    kh_run_text_wrap_tests();
    kh_run_layout_tests();
    kh_run_canvas_tests();
    kh_run_gesture_tests();
    kh_run_input_tests();
    kh_run_lipc_parse_tests();
    kh_run_lipc_watch_tests();
    kh_run_state_tests();
    kh_run_tabs_tests();
    kh_run_urledit_tests();
    kh_run_kindletimes_tests();
    kh_run_favorites_tests();
    kh_run_gaps_tests();
    kh_run_news_tests();
    kh_run_browser_row_tests();
    kh_run_browser_orientation_tests();
    kh_run_long_press_tests();
    kh_summary();
    return kh_tests_failed == 0 ? 0 : 1;
}
