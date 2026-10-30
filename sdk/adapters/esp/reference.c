#include "sdk/dsp.h"
#include "sdk_config.h"
#include <limits.h>
#ifdef CONFIG_SDK_ESP_DSP_REF
#include <dsps_dotprod.h>
#endif
#ifdef CONFIG_SDK_ESP_NN_REF
void esp_nn_relu6_s8_ansi(int8_t *data, uint16_t size);
#endif
int sdk_dsp_dot_f32(const float *a, const float *b, size_t count, float *result)
{
#ifdef CONFIG_SDK_ESP_DSP_REF
	if (!a || !b || !result || !count || count > INT_MAX)
		return -1;
	return dsps_dotprod_f32_ansi(a, b, result, (int)count) == 0 ? 0 : -1;
#else
	(void)a;
	(void)b;
	(void)count;
	(void)result;
	return -1;
#endif
}
int sdk_nn_relu6_s8(int8_t *data, size_t count)
{
#ifdef CONFIG_SDK_ESP_NN_REF
	if (!data || !count || count > UINT16_MAX)
		return -1;
	esp_nn_relu6_s8_ansi(data, (uint16_t)count);
	return 0;
#else
	(void)data;
	(void)count;
	return -1;
#endif
}
