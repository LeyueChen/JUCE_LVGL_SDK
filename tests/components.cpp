#include "sdk_config.h"
#include "storage_backend.h"
#include "sdk/dsp.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#ifdef CONFIG_SDK_CJSON
#include <cJSON.h>
#endif
#ifdef CONFIG_SDK_NANOPB
#include <pb_encode.h>
#include <pb_decode.h>
#include "test_heartbeat.pb.h"
#endif
#ifdef CONFIG_SDK_MULTIBUTTON
#include <multi_button.h>
#endif
#ifdef CONFIG_SDK_EASYLOGGER
#include <elog.h>
#include "sdk/log.h"
static char captured[1024];
static void capture(const char *text, size_t size)
{
	std::strncat(captured, text,
		     size < sizeof(captured) - std::strlen(captured) - 1 ?
			     size :
			     sizeof(captured) - std::strlen(captured) - 1);
}
#endif
#ifdef CONFIG_SDK_RTNEURAL
#include <RTNeural/RTNeural.h>
#endif
#define REQUIRE(condition)                                    \
	do {                                                  \
		if (!(condition))                             \
			throw std::runtime_error(#condition); \
	} while (0)
#ifdef CONFIG_SDK_MULTIBUTTON
static uint8_t level = 1;
static unsigned presses = 0;
static uint8_t read_button(uint8_t)
{
	return level;
}
static void pressed(Button *, void *)
{
	++presses;
}
#endif
int main(int argc, char **argv)
{
	try {
#ifdef CONFIG_SDK_CJSON
		cJSON *json = cJSON_Parse("{\"cpu\":0,\"reserved\":[1,2,3]}");
		REQUIRE(json && cJSON_GetObjectItemCaseSensitive(json, "cpu")
						->valueint == 0);
		REQUIRE(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(
				json, "reserved")) == 3);
		cJSON_Delete(json);
		REQUIRE(cJSON_Parse("{broken") == nullptr);
		std::puts("PASS cJSON valid and invalid input");
#endif
#ifdef CONFIG_SDK_NANOPB
		uint8_t bytes[16] = {};
		auto output = pb_ostream_from_buffer(bytes, sizeof(bytes));
		REQUIRE(pb_encode_tag(&output, PB_WT_VARINT, 1));
		REQUIRE(pb_encode_varint(&output, 123456));
		auto input =
			pb_istream_from_buffer(bytes, output.bytes_written);
		pb_wire_type_t wire;
		uint32_t field;
		bool eof;
		uint64_t value;
		REQUIRE(pb_decode_tag(&input, &wire, &field, &eof) &&
			field == 1 && wire == PB_WT_VARINT);
		REQUIRE(pb_decode_varint(&input, &value) && value == 123456);
		std::puts("PASS nanopb wire encode/decode");
		sdk_test_Heartbeat proto_message = sdk_test_Heartbeat_init_zero;
		proto_message.logical_core = 0;
		proto_message.sequence = 42;
		std::strcpy(proto_message.label, "CPU0");
		uint8_t protocol_bytes[128] = {};
		auto protocol_output = pb_ostream_from_buffer(
			protocol_bytes, sizeof(protocol_bytes));
		REQUIRE(pb_encode(&protocol_output, sdk_test_Heartbeat_fields,
				  &proto_message));
		sdk_test_Heartbeat restored_message =
			sdk_test_Heartbeat_init_zero;
		auto protocol_input = pb_istream_from_buffer(
			protocol_bytes, protocol_output.bytes_written);
		REQUIRE(pb_decode(&protocol_input, sdk_test_Heartbeat_fields,
				  &restored_message));
		REQUIRE(restored_message.sequence == 42 &&
			std::strcmp(restored_message.label, "CPU0") == 0);
		std::puts("PASS generated nanopb schema");
#endif
#ifdef CONFIG_SDK_MULTIBUTTON
		Button button{};
		button_init(&button, read_button, 0, 0);
		button_attach(&button, BTN_PRESS_DOWN, pressed, nullptr);
		REQUIRE(button_start(&button) == 0);
		level = 0;
		for (int i = 0; i < DEBOUNCE_TICKS + 3; ++i)
			button_ticks();
		REQUIRE(presses == 1);
		button_stop(&button);
		std::puts("PASS MultiButton debounced event");
#endif
#ifdef CONFIG_SDK_EASYLOGGER
		sdk_elog_set_sink(capture);
		REQUIRE(elog_init() == ELOG_NO_ERR);
		elog_set_fmt(ELOG_LVL_INFO, ELOG_FMT_TAG);
		elog_start();
		elog_i("test", "CPU%d ready", 0);
		REQUIRE(std::strstr(captured, "CPU0 ready"));
		std::puts("PASS EasyLogger output adapter");
#endif
#ifdef CONFIG_SDK_RTNEURAL
		RTNeural::DenseT<float, 2, 1> dense;
		dense.setWeights(
			std::vector<std::vector<float> >{ { 2.0f, 3.0f } });
		const float bias[] = { 1.0f };
		const float neural_input[] = { 1.0f, 2.0f };
		dense.setBias(bias);
		dense.forward(neural_input);
		REQUIRE(std::fabs(dense.outs[0] - 9.0f) < 1.0e-6f);
		std::puts("PASS RTNeural STL dense inference");
#endif
#ifdef CONFIG_SDK_ESP_DSP_REF
		const float dsp_a[] = { 1, 2, 3 }, dsp_b[] = { 4, 5, 6 };
		float dot = 0;
		REQUIRE(sdk_dsp_dot_f32(dsp_a, dsp_b, 3, &dot) == 0 &&
			std::fabs(dot - 32.0f) < 1.0e-6f);
		REQUIRE(sdk_dsp_dot_f32(nullptr, dsp_b, 3, &dot) < 0);
		std::puts("PASS ESP-DSP ANSI reference dot product");
#endif
#ifdef CONFIG_SDK_ESP_NN_REF
		int8_t activations[] = { -10, 0, 3, 6, 100 };
		const int8_t expected_activations[] = { 0, 0, 3, 6, 6 };
		REQUIRE(sdk_nn_relu6_s8(activations, 5) == 0 &&
			std::memcmp(activations, expected_activations, 5) == 0);
		std::puts("PASS ESP-NN ANSI reference ReLU6");
#endif
		REQUIRE(argc == 2);
		void *backend = sdk_backend_open(argv[1]);
		REQUIRE(backend);
		const char message[] = "persistent SDK data";
#ifdef CONFIG_SDK_LITTLEFS
		REQUIRE(sdk_backend_file_write(backend, "/settings.txt",
					       message, sizeof(message)) == 0);
#endif
#ifdef CONFIG_SDK_FLASHDB
		REQUIRE(sdk_backend_kv_set(backend, "test", message,
					   sizeof(message)) == 0);
#endif
		sdk_backend_close(backend);
		backend = sdk_backend_open(argv[1]);
		REQUIRE(backend);
		char restored[64] = {};
#ifdef CONFIG_SDK_LITTLEFS
		REQUIRE(sdk_backend_file_read(backend, "/settings.txt",
					      restored, sizeof(restored)) ==
			sizeof(message));
		REQUIRE(std::strcmp(restored, message) == 0);
		std::puts("PASS littlefs persistence across unmount/remount");
#endif
#ifdef CONFIG_SDK_FLASHDB
		REQUIRE(sdk_backend_kv_get(backend, "test", restored,
					   sizeof(restored)) ==
			sizeof(message));
		REQUIRE(std::strcmp(restored, message) == 0);
		std::puts("PASS FlashDB persistence across deinit/reopen");
#endif

		sdk_storage_request_t request{};
		sdk_storage_result_t result{};
#ifdef CONFIG_SDK_LITTLEFS
		request.operation = SDK_STORAGE_FILE_MKDIR;
		std::strcpy(request.key, "/resources");
		sdk_backend_execute(backend, &request, &result);
		REQUIRE(!result.result);
		std::strcpy(request.key, "/resources/model.tmp");
		for (unsigned part = 0; part < 3; ++part) {
			request.operation = part ? SDK_STORAGE_FILE_WRITE_AT :
						   SDK_STORAGE_FILE_WRITE;
			request.offset = part * 256;
			request.size = 256;
			std::memset(request.data, int(part + 10), 256);
			sdk_backend_execute(backend, &request, &result);
			REQUIRE(!result.result && result.size == 256);
		}
		request.operation = SDK_STORAGE_FILE_RENAME;
		std::strcpy(request.destination, "/resources/model.bin");
		sdk_backend_execute(backend, &request, &result);
		REQUIRE(!result.result);
		std::strcpy(request.key, request.destination);
		request.operation = SDK_STORAGE_FILE_SIZE;
		sdk_backend_execute(backend, &request, &result);
		REQUIRE(!result.result && result.size == 768);
		request.operation = SDK_STORAGE_FILE_READ_AT;
		request.offset = 256;
		sdk_backend_execute(backend, &request, &result);
		REQUIRE(!result.result && result.size == 256);
		for (auto value : result.data)
			REQUIRE(value == 11);
		request.operation = SDK_STORAGE_FILE_REMOVE;
		sdk_backend_execute(backend, &request, &result);
		REQUIRE(!result.result);
		request.operation = SDK_STORAGE_FILE_SIZE;
		sdk_backend_execute(backend, &request, &result);
		REQUIRE(result.result == SDK_STORAGE_NOT_FOUND);
		std::puts("PASS littlefs chunked resources/rename/size/remove");
#endif
#ifdef CONFIG_SDK_FLASHDB
		request = {};
		std::strcpy(request.key, "test");
		request.operation = SDK_STORAGE_KV_SIZE;
		sdk_backend_execute(backend, &request, &result);
		REQUIRE(!result.result && result.size == sizeof(message));
		request.operation = SDK_STORAGE_KV_GET;
		request.size = 1;
		sdk_backend_execute(backend, &request, &result);
		REQUIRE(result.result == SDK_STORAGE_BUFFER_TOO_SMALL);
		request.operation = SDK_STORAGE_KV_DELETE;
		sdk_backend_execute(backend, &request, &result);
		REQUIRE(!result.result);
		request.operation = SDK_STORAGE_KV_GET;
		sdk_backend_execute(backend, &request, &result);
		REQUIRE(result.result == SDK_STORAGE_NOT_FOUND);
		std::puts("PASS FlashDB size/short-buffer/delete/missing");
#endif
		request = {};
		request.operation = SDK_STORAGE_FILE_READ;
		std::strcpy(request.key, "/resources/../secret");
		sdk_backend_execute(backend, &request, &result);
		REQUIRE(result.result == SDK_STORAGE_INVALID);
		sdk_backend_close(backend);
		return 0;
	} catch (const std::exception &error) {
		std::fprintf(stderr, "FAIL %s\n", error.what());
		return 1;
	}
}
