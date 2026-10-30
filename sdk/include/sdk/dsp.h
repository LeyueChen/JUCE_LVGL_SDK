#ifndef SDK_DSP_H
#define SDK_DSP_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int sdk_dsp_dot_f32(const float *a, const float *b, size_t count,
		    float *result);
/* Integer ReLU6 clamps samples to [0, 6]; this is not a model runtime. */
int sdk_nn_relu6_s8(int8_t *data, size_t count);
#ifdef __cplusplus
}
#endif
#endif
