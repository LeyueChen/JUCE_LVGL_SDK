#include "rea/rea.h"
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void check(bool ok)
{
	if (!ok)
		abort();
}

int main(void)
{
	rea_chain_t *chain = rea_chain_create();
	check(chain != NULL);
	check(rea_chain_append_gain(chain, 0.5f) == 0);
	check(!rea_chain_prepare(chain, 48000, 2));
	float samples[] = { 0.8f, -0.8f, 0.4f, -0.4f };
	check(!rea_chain_process(chain, samples, 2, 2));
	check(fabsf(samples[0] - 0.4f) < 1e-6f);
	check(!rea_chain_parameter(chain, 0, 0, 2));
	check(rea_chain_parameter(chain, 0, 0, NAN) < 0);
	check(!rea_chain_bypass(chain, 0, 1));
	check(!rea_chain_process(chain, samples, 2, 2) &&
	      fabsf(samples[0] - 0.4f) < 1e-6f);
	check(rea_chain_append_soft_clip(chain, 3) == 1);
	rea_preset_t preset = { 0 };
	check(!rea_chain_export(chain, &preset));
	check(!rea_chain_import(chain, &preset));
	rea_preset_t bad = preset;
	bad.nodes[0].type = 999;
	check(rea_chain_import(chain, &bad) < 0);
	rea_preset_t retained = { 0 };
	check(!rea_chain_export(chain, &retained));
	check(retained.count == preset.count &&
	      retained.nodes[0].type == preset.nodes[0].type);
	samples[0] = 0.4f;
	check(!rea_chain_process(chain, samples, 2, 2));
	check(fabsf(samples[0] - tanhf(1.2f)) < 1e-6f);
	check(rea_chain_process(chain, samples, 2, 1) < 0);
	rea_chain_clear(chain);
#if REA_USE_RTNEURAL
	const char model[] =
		"{\"in_shape\":[null,1],\"layers\":[{\"type\":\"dense\","
		"\"shape\":[null,1],\"activation\":\"linear\",\"weights\":[[[0.5]],[0.1]]}]}";
	check(rea_chain_append_rtneural(chain, model, sizeof(model) - 1) == 0);
	check(rea_chain_append_rtneural(chain, "{}", 2) < 0);
	const char malformed[] =
		"{\"in_shape\":[null,1],\"layers\":[{\"type\":\"dense\","
		"\"shape\":[null,1],\"weights\":[[[1]],[]]}]}";
	check(rea_chain_append_rtneural(chain, malformed,
					sizeof(malformed) - 1) < 0);
	check(!rea_chain_prepare(chain, 48000, 2));
	samples[0] = 0.8f;
	samples[1] = -0.8f;
	check(!rea_chain_process(chain, samples, 1, 2));
	check(fabsf(samples[0] - 0.5f) < 1e-6f &&
	      fabsf(samples[1] + 0.3f) < 1e-6f);
#else
	check(rea_chain_append_rtneural(chain, "{}", 2) < 0);
#endif
	rea_chain_destroy(chain);
	puts("PASS C effect lifecycle/parameters/bypass/presets/RTNeural");
	return 0;
}
