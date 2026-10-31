#include "rea/rea_audio.h"
#include "sdk/storage.h"
#include "sdk/log.h"
#include "sdk_config.h"
#include "FreeRTOS.h"
#include <string.h>
/* Preset contexts cross AudioTask/StorageTask only via copied requests and
 * ownership transfer. Neither callback accesses LVGL. */
#if defined(CONFIG_SDK_FLASHDB) || defined(CONFIG_SDK_LITTLEFS)
static sdk_storage_operation_t preset_operation(int write)
{
#ifdef CONFIG_SDK_FLASHDB
	return write ? SDK_STORAGE_KV_SET : SDK_STORAGE_KV_GET;
#else
	return write ? SDK_STORAGE_FILE_WRITE : SDK_STORAGE_FILE_READ;
#endif
}
static void preset_key(char *key)
{
#ifdef CONFIG_SDK_FLASHDB
	strcpy(key, "effects.preset");
#else
	strcpy(key, "/effects.preset");
#endif
}
static int export_preset(rea_chain_t *chain, void *context)
{
	return rea_chain_export(chain, context);
}
static int import_preset(rea_chain_t *chain, void *context)
{
	return rea_chain_import(chain, context);
}
static void imported(int result, void *context)
{
	sdk_log_text(result ? "Preset rejected" : "Preset loaded on AudioTask");
	vPortFree(context);
}
static void exported(int result, void *context)
{
	if (!result) {
		sdk_storage_request_t request = { 0 };
		request.operation = preset_operation(1);
		preset_key(request.key);
		request.size = sizeof(rea_preset_t);
		memcpy(request.data, context, request.size);
		if (sdk_storage_submit(&request, NULL, NULL))
			sdk_log_text("Preset storage queue full");
	}
	vPortFree(context);
}
static void loaded(const sdk_storage_result_t *result, void *unused)
{
	(void)unused;
	if (result->result || result->size != sizeof(rea_preset_t)) {
		sdk_log_text("No valid saved preset");
		return;
	}
	rea_preset_t *preset = pvPortMalloc(sizeof(*preset));
	if (!preset)
		return;
	memcpy(preset, result->data, sizeof(*preset));
	if (rea_audio_control(import_preset, preset, imported))
		vPortFree(preset);
}
static void preset_button(sdk_button_id_t button)
{
	if (button == SDK_BUTTON_A) {
		rea_preset_t *preset = pvPortMalloc(sizeof(*preset));
		if (preset &&
		    rea_audio_control(export_preset, preset, exported))
			vPortFree(preset);
	} else if (button == SDK_BUTTON_B) {
		sdk_storage_request_t request = { 0 };
		request.operation = preset_operation(0);
		preset_key(request.key);
		request.size = sizeof(rea_preset_t);
		if (sdk_storage_submit(&request, loaded, NULL))
			sdk_log_text("Preset load queue full");
	}
}
#else
static void preset_button(sdk_button_id_t button)
{
	(void)button;
}
#endif
static int change_gain(rea_chain_t *chain, void *context)
{
	return rea_chain_parameter(chain, 0, 0, *(float *)context);
}
static void gain_changed(int result, void *context)
{
	(void)result;
	vPortFree(context);
}
void rea_audio_handle_midi(const sdk_midi_message_t *message)
{
	/* MIDI CC7 controls the first gain node through an AudioTask command. */
	if (!message || message->size != 3 ||
	    (message->data[0] & 0xf0) != 0xb0 || message->data[1] != 7)
		return;
	float *gain = pvPortMalloc(sizeof(*gain));
	if (!gain)
		return;
	*gain = message->data[2] / 127.0f;
	if (rea_audio_control(change_gain, gain, gain_changed))
		vPortFree(gain);
}

void rea_audio_handle_button(const sdk_button_event_t *event)
{
	if (event && event->action == SDK_BUTTON_CLICK)
		preset_button(event->button);
}
