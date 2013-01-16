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

	ret = pread(shr->fd, &flag, sizeof(char), 0);
	if (ret == -1)
		__ERROR("Read error when open_shrink");

	shr->idx->flag = flag;
}

int _check_shrinking(struct shrink *shr)
{
	char slave_name[NESSDB_PATH_SIZE];

	memset(slave_name, 0, NESSDB_PATH_SIZE);
	snprintf(slave_name, NESSDB_PATH_SIZE, "%s/%s", shr->idx->path, NESSDB_DB_SLAVE);

	shr->idx->fd_slave = n_open(slave_name, N_OPEN_FLAGS, 0644);
	/*
	 * If fd_slave > -1, it's interrupted when shrinking
	 * Must sync-shrink again when database openned
	 */
	if (shr->idx->fd_slave > -1)
		return 1;

	return 0;
}

void _exchange_fd(struct shrink *shr)
{
	int magic;
	char db_name[NESSDB_PATH_SIZE];
	char slave_name[NESSDB_PATH_SIZE];

	memset(slave_name, 0, NESSDB_PATH_SIZE);
	snprintf(slave_name, NESSDB_PATH_SIZE, "%s/%s", shr->idx->path, NESSDB_DB_SLAVE);

	memset(db_name, 0, NESSDB_PATH_SIZE);
	snprintf(db_name, NESSDB_PATH_SIZE, "%s/%s", shr->idx->path, NESSDB_DB);

	/*
	 * When datafile is writing, wait write done.
	 * Then catch the write lock, and rename the master datafile(ness.DB) to slave (ness.SLAVE)
	 * And create the new master file:ness.DB
	 */
	pthread_mutex_lock(shr->idx->w_lock);
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

	shr->idx->flag = (shr->idx->flag == 0 ? 1:0);
	if (pwrite(shr->fd, &shr->idx->flag, sizeof(char), 0) == -1)
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
	pthread_mutex_unlock(shr->idx->w_lock);
}

void *_shrink_job(void *arg)
{
	int node_size;
	struct shrink *shr;
	struct meta_node *nodes;

	__DEBUG("---->Background shrinking start.....");
	shr = (struct shrink*)arg;
	node_size = shr->idx->meta->size;
	nodes = xcalloc(node_size, sizeof(struct meta_node));

	memcpy(nodes, shr->idx->meta->nodes, node_size * sizeof(struct meta_node));
	//TODO iteration the SSTs and rewrite to nessDB,via meta
	xfree(nodes);

	//Remove slave db

	__DEBUG("---->Background shrinking end.....");
	if (shr->async) {
		pthread_detach(pthread_self());
		pthread_exit(NULL);
	} else
		return NULL;
}

struct shrink *shrink_new(struct index *idx)
{
	struct shrink *shr = xcalloc(1, sizeof(struct shrink));

	shr->idx = idx;
	_open_shrink(shr);

	if (_check_shrinking(shr)) {
		shr->async = 0;
		_shrink_job(shr);
	}

	return shr;
}

void shrink_do(struct shrink *shr)
{
	pthread_t tid;

	/*
	 * Exchange the fd between MASTER and SLAVE data files
	 */
	_exchange_fd(shr);

	shr->async = 1;
	pthread_create(&tid, &shr->attr, _shrink_job, shr);
}

void shrink_free(struct shrink *shr)
{
	xfree(shr);
}
