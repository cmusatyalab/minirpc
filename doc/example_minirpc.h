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

/* The application should never need to use these directly. */
/**
 * @cond SHOW_PROCEDURE_ENUM
 */
enum example_server_procedures {
	nr_example_ChooseColor = 1,
	nr_example_GetNumColors = 2,
	nr_example_CrayonSelected = -1,
};
/**
 * @endcond
 */

/**
 * @brief Free a Color
 * @param	in
 *	The Color to free
 * @param	container
 *	Whether to free the container, or just the contents
 *
 * Free all memory pointed to by members of @c in.  If @c container is nonzero,
 * also free @c in.
 */
void free_Color(Color *in, int container);

/**
 * @brief Free a ColorChoice
 * @param	in
 *	The ColorChoice to free
 * @param	container
 *	Whether to free the container, or just the contents
 *
 * Free all memory pointed to by members of @c in.  If @c container is nonzero,
 * also free @c in.
 */
void free_ColorChoice(ColorChoice *in, int container);

/**
 * @brief Free a Count
 * @param	in
 *	The Count to free
 * @param	container
 *	Whether to free the container, or just the contents
 *
 * Free all memory pointed to by members of @c in.  If @c container is nonzero,
 * also free @c in.
 */
void free_Count(Count *in, int container);

/**
 * @}
 * @}
 */

#endif