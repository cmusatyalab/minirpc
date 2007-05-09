#include "minirpc.h"

struct sample_client_operations {
	methods...
};

enum sample_client_procedures {
	nr_list_parcels = 17,
};

extern struct mrpc_protocol sample_client;
