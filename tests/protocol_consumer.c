#include <pb_encode.h>
#include <pb_decode.h>
#include "heartbeat.pb.h"

int main(void)
{
	sdk_demo_Heartbeat source = sdk_demo_Heartbeat_init_zero;
	sdk_demo_Heartbeat decoded = sdk_demo_Heartbeat_init_zero;
	source.logical_core = 0;
	source.sequence = 42;
	pb_byte_t bytes[64];
	pb_ostream_t output = pb_ostream_from_buffer(bytes, sizeof(bytes));
	if (!pb_encode(&output, sdk_demo_Heartbeat_fields, &source))
		return 1;
	pb_istream_t input =
		pb_istream_from_buffer(bytes, output.bytes_written);
	if (!pb_decode(&input, sdk_demo_Heartbeat_fields, &decoded))
		return 2;
	return decoded.sequence != source.sequence ||
	       decoded.logical_core != source.logical_core;
}
