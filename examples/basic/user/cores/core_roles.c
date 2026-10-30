#include "sdk/core.h"
void core_cpu_0(const sdk_core_context_t *);
void core_cpu_1(const sdk_core_context_t *);
void core_cpu_2(const sdk_core_context_t *);
void core_cpu_3(const sdk_core_context_t *);
const sdk_core_roles_t *user_core_roles(void)
{
	static const sdk_core_roles_t roles = { { core_cpu_0, core_cpu_1,
						  core_cpu_2, core_cpu_3 } };
	return &roles;
}
