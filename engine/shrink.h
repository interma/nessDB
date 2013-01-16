#ifndef __nessDB_SHRINK_H
#define __nessDB_SHRINK_H

#include "index.h"

struct shrink {
	int fd;
	int async;
	struct index *idx;
	pthread_attr_t attr;
};

struct shrink *shrink_new(struct index *idx);
void shrink_do(struct shrink *shr);
void shrink_free(struct shrink *shr);

#endif
