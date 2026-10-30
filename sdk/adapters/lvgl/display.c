#include <lvgl.h>
#include "sdk/ui.h"
#include "sdk/os.h"
#include "sdk/host_bridge.h"
#include "sdk_config.h"
#include <string.h>
static uint32_t framebuffer[CONFIG_SDK_SCREEN_WIDTH * CONFIG_SDK_SCREEN_HEIGHT];
static uint32_t draw_buffer[CONFIG_SDK_SCREEN_WIDTH * 64];
static uint32_t tick_ms(void) { return (uint32_t)sdk_uptime_ms(); }
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    int width = lv_area_get_width(area);
    for (int y = area->y1; y <= area->y2; ++y) {
        memcpy(framebuffer + y * CONFIG_SDK_SCREEN_WIDTH + area->x1,
               pixels + (y - area->y1) * width * 4, (size_t)width * 4);
    }
    if (lv_display_flush_is_last(display)) sdk_host_publish_frame(framebuffer, sizeof(framebuffer));
    lv_display_flush_ready(display);
}
static void pointer_read(lv_indev_t *device, lv_indev_data_t *data) {
    (void)device;
    sdk_pointer_t pointer = sdk_host_pointer();
    data->point.x = pointer.x; data->point.y = pointer.y;
    data->state = pointer.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
void sdk_ui_run(void *unused) {
    (void)unused;
    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_display_t *display = lv_display_create(CONFIG_SDK_SCREEN_WIDTH, CONFIG_SDK_SCREEN_HEIGHT);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, pointer_read);
    user_ui_create();
    while (!sdk_host_stop_requested()) {
        user_ui_tick();
        uint32_t wait = lv_timer_handler();
        sdk_delay_ms(wait > 16 ? 16 : (wait ? wait : 1));
    }
}
