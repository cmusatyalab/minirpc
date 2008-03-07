/* AUTOGENERATED FILE -- DO NOT EDIT */

/**
 * @file
 * @brief Common declarations for the example protocol
 *
 * @defgroup example Example Protocol
 * @{
 *
 * @include example.mx
 *
 * @defgroup example_common Common Definitions
 * @{
 */

#ifndef EXAMPLE_MINIRPC_H
#define EXAMPLE_MINIRPC_H

#include "example_xdr.h"

enum example_server_procedures {
	nr_example_ChooseColor = 1,
	nr_example_GetNumColors = 2,
	nr_example_CrayonSelected = -1,
};

void free_Color(Color *in, int container);
void free_ColorChoice(ColorChoice *in, int container);
void free_Count(Count *in, int container);

/**
 * @}
 * @}
 */

#endif
