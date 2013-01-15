#include "internal.h"
#include "shrink.h"
#include "xmalloc.h"
#include "debug.h"

#define SHRINK_NAME ("ness.SHRINK")
void _open_shrink(struct shrink *shr)
{
	int ret;
	char flag = 0;
	char shr_name[NESSDB_PATH_SIZE];

	memset(shr_name, 0, NESSDB_PATH_SIZE);
	snprintf(shr_name, NESSDB_PATH_SIZE, "%s/%s", shr->idx->path, SHRINK_NAME);

	shr->fd = n_open(shr_name, N_OPEN_FLAGS, 0644);

	if (shr->fd == -1) 
		shr->fd = n_open(shr_name, N_CREAT_FLAGS, 0644);

	ret = pread(shr->fd, flag, sizeof(char), 0);
	if (ret == -1)
		__ERROR("Read error when open_shrink");

	shr->idx->flag = flag;
}

//TODO: lock
void _exchange(struct shrink *shr)
{
	int magic;
	char db_name[NESSDB_PATH_SIZE];
	char slave_name[NESSDB_PATH_SIZE];

	memset(slave_name, 0, NESSDB_PATH_SIZE);
	snprintf(slave_name, NESSDB_PATH_SIZE, "%s/%s", shr->idx->path, NESSDB_DB_SLAVE);

	memset(db_name, 0, NESSDB_PATH_SIZE);
	snprintf(db_name, NESSDB_PATH_SIZE, "%s/%s", shr->idx->path, NESSDB_DB);

	if (rename(db_name, slave_name) == -1)
		__PANIC("Error when shrink@exchange");

	if (shr->idx->fd > 0)
		close(shr->idx->fd);

	shr->idx->fd = n_open(db_name, N_OPEN_FLAGS, 0644);
	if (shr->idx->fd == -1) {
		shr->idx->db_alloc = 0;
		shr->idx->fd = n_open(db_name, N_CREAT_FLAGS, 0644);
		if (shr->idx->fd == -1) 
			__PANIC("db error, name#%s", 
					db_name);

		magic = DB_MAGIC;
		if (write(shr->idx->fd, &magic, sizeof(magic)) < 0)
			__PANIC("write db magic error");
		shr->idx->db_alloc += sizeof(magic);
	}

	shr->idx->flag = ((shr->idx->flag == 0)?1:0);
	if (pwrite(shr->fd, shr->idx->flag, sizeof(char), 0) == -1)
		__PANIC("Write flag to SHRINK...");

	if (shr->idx->read_fd > 0)
		close(shr->idx->read_fd);
	shr->idx->read_fd = n_open(db_name, N_OPEN_FLAGS);
	
	if (shr->idx->db_alloc> 0)
		close(shr->idx->db_alloc);
	shr->idx->db_alloc = n_lseek(shr->idx->fd, 0, SEEK_END);

	if (shr->idx->read_fd_slave > 0)
		close(shr->idx->read_fd_slave);
	shr->idx->read_fd_slave = n_open(slave_name, N_OPEN_FLAGS);

	if (shr->idx->fd_slave > 0)
		close(shr->idx->fd_slave);
	shr->idx->fd_slave = n_open(slave_name, N_OPEN_FLAGS, 0644);
}

struct shrink *shrink_new(struct index *idx)
{
	struct shrink *shr = xcalloc(1, sizeof(struct shrink));

	shr->idx = idx;
	_open_shrink(shr);

	return shr;
}

void shrink_do(struct shrink *shr)
{
	_exchange(shr);
	//TODO iteration the SSTs and rewrite to nessDB
}

void shrink_free(struct shrink *shr)
{
	xfree(shr);
}
