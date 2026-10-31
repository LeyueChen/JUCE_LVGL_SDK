#ifndef REA_H
#define REA_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {
	REA_MAX_NODES = 8,
	REA_MAX_TYPES = 16,
	REA_GAIN = 1,
	REA_SOFT_CLIP = 2,
	REA_RTNEURAL = 100
};
typedef struct rea_chain rea_chain_t;
/* Single-owner portable processing engine. No JUCE or RTOS dependency. Register
 * immutable descriptors before append; descriptor lifetime exceeds the chain.
 * On successful append the chain owns context and invokes destroy at clear.
 * prepare/reset/parameter/process execute on the chain owner. Process callbacks
 * operate on interleaved PCM without blocking, allocation or I/O. */
typedef struct {
	uint32_t type_id;
	int (*prepare)(void *context, uint32_t sample_rate, unsigned channels);
	void (*reset)(void *context);
	void (*process)(void *context, float *samples, unsigned frames,
			unsigned channels);
	int (*parameter)(void *context, unsigned id, float value);
	void (*destroy)(void *context);
} rea_node_type_t;
rea_chain_t *rea_chain_create(void);
void rea_chain_destroy(rea_chain_t *chain);
void rea_chain_clear(rea_chain_t *chain);
int rea_chain_register(rea_chain_t *chain, const rea_node_type_t *type);
int rea_chain_append(rea_chain_t *chain, uint32_t type, void *context);
/* Returns node index, or negative on invalid/full. Gain parameter 0: [0,4].
 * Soft clip parameter 0 is drive [0.1,20], output = tanh(drive * input). */
int rea_chain_append_gain(rea_chain_t *chain, float gain);
int rea_chain_append_soft_clip(rea_chain_t *chain, float drive);
int rea_chain_parameter(rea_chain_t *chain, unsigned node, unsigned parameter,
			float value);
int rea_chain_bypass(rea_chain_t *chain, unsigned node, int bypass);
int rea_chain_prepare(rea_chain_t *chain, uint32_t sample_rate,
		      unsigned channels);
void rea_chain_reset(rea_chain_t *chain);
int rea_chain_process(rea_chain_t *chain, float *samples, unsigned frames,
		      unsigned channels);
/* Versioned preset for built-in gain/soft-clip nodes; large records can use
 * storage WRITE_AT chunks. Custom/neural models are separate resource files.
 * Import rejects unsupported types without modifying the existing chain. */
typedef struct {
	uint32_t type, bypass;
	float value;
} rea_preset_node_t;
typedef struct {
	uint32_t version, count;
	rea_preset_node_t nodes[REA_MAX_NODES];
} rea_preset_t;
int rea_chain_export(const rea_chain_t *chain, rea_preset_t *preset);
int rea_chain_import(rea_chain_t *chain, const rea_preset_t *preset);
/* Optional RTNeural STL node: validated mono-in/mono-out dense networks, one
 * independent model per audio channel. RTNeural JSON in_shape/layers format;
 * dense + tanh/relu/sigmoid/linear activation, <=16 layers, <=64 units/layer.
 * Parsing/allocation occurs on the control path, never the JUCE callback. */
int rea_chain_append_rtneural(rea_chain_t *chain, const char *json,
			      size_t size);
#ifdef __cplusplus
}
#endif
#endif
