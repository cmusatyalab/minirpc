#define MINIRPC_INTERNAL
#include "minirpc_protocol.h"
#include "sample-client.h"

void free_ListParcels(ListParcels *in, int container)
{
	xdr_free(xdr_ListParcels, in);
	if (container)
		free(in);
}
