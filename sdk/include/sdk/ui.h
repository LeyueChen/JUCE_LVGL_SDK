#ifndef SDK_UI_H
#define SDK_UI_H
#ifdef __cplusplus
extern "C" {
#endif
void sdk_ui_run(void *unused);
void sdk_ui_record_event(void);
void user_ui_create(void);
void user_ui_tick(void);
void user_app_start(void);
#ifdef __cplusplus
}
#endif
#endif
