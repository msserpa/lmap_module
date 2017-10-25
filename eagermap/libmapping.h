#ifndef __LIBMAPPING_H__
#define __LIBMAPPING_H__


#include <linux/slab.h>

#ifndef MAX_THREADS
	#define MAX_THREADS 256
#endif
#define PRINTF_PREFIX "libmapping: "

typedef u64 uint64_t;


#include "lib.h"
#include "graph.h"
#include "mapping.h"
#include "topology.h"
#include "connect.h"
#include "mapping-algorithms.h"


#endif
