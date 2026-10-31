#include "media.h"
#include "sdk/midi.h"
#include "sdk/input.h"
#include "sdk/ui.h"
#include "sdk/os.h"
#include "sdk/log.h"
#include "sdk/system.h"
#include "sdk_config.h"
extern void user_button_event(const void *, size_t, void *);
static void media_task(void *unused)
{
	(void)unused;
	while (!sdk_system_is_stopping()) {
		sdk_midi_message_t message;
		for (unsigned i = 0; i < 16 && sdk_midi_read(&message) == 1;
		     ++i)
			sdk_log_text("MIDI received on CPU0");
		sdk_button_event_t event;
		for (unsigned i = 0; i < 16 && sdk_button_read(&event) == 1;
		     ++i)
			if (sdk_ui_post(user_button_event, &event,
					sizeof(event), 0))
				sdk_log_text("UI button queue full");
		sdk_delay_ms(5);
	}
}
void user_media_start(void)
{
#if defined(CONFIG_SDK_MIDI) || defined(CONFIG_SDK_MULTIBUTTON)
	const sdk_task_config_t task = { "MediaTask", 16384, 2, 0 };
	if (sdk_task_create(&task, media_task, 0))
		sdk_assert_failed(__FILE__, __LINE__);
#endif
}
