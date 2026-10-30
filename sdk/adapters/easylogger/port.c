#include <elog.h>
#include "sdk/log.h"
/* EasyLogger is owned by the SDK LogTask; callers enqueue through sdk_log_text. */
static sdk_log_sink_fn output_sink;
void sdk_elog_set_sink(sdk_log_sink_fn sink)
{
	output_sink = sink;
}
ElogErrCode elog_port_init(void)
{
	return ELOG_NO_ERR;
}
void elog_port_output(const char *text, size_t size)
{
	if (output_sink)
		output_sink(text, size);
}
void elog_port_output_lock(void)
{
}
void elog_port_output_unlock(void)
{
}
const char *elog_port_get_time(void)
{
	return "";
}
const char *elog_port_get_p_info(void)
{
	return "sdk";
}
const char *elog_port_get_t_info(void)
{
	return "log";
}
