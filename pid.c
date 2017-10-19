#include <linux/module.h>
#include <linux/hashtable.h>

#define lmap_PID_HASH_BITS 14UL
#define lmap_PID_HASH_SIZE (1UL << lmap_PID_HASH_BITS)

struct pid_s{
	int pid;
	int tid;
};

extern int do_tm; /* thread mapping */
/* extern void set_aff(int pid, int tid); */

static struct pid_s lmap_pid[lmap_PID_HASH_SIZE];
static struct pid_s lmap_pid_reverse[lmap_PID_HASH_SIZE];

static atomic_t lmap_num_threads = ATOMIC_INIT(0);
static atomic_t lmap_active_threads = ATOMIC_INIT(0);

int lmap_get_pid(int tid){
	return lmap_pid_reverse[tid].pid;
}

int lmap_get_tid(int pid){
	unsigned h = hash_32(pid, lmap_PID_HASH_BITS);

	if(lmap_pid[h].pid == pid)
		return lmap_pid[h].tid;

	return -1;
}

int lmap_get_num_threads(void){
	return atomic_read(&lmap_num_threads);
}

int lmap_get_active_threads(void){
	return atomic_read(&lmap_active_threads);
}

int lmap_delete_pid(int pid){
	int at = -1;
	unsigned h = hash_32(pid, lmap_PID_HASH_BITS);
	int tid = lmap_pid[h].tid;

	if(tid != -1 && lmap_pid[h].pid == pid){
		lmap_pid[h].tid = -1;
		lmap_pid[h].pid = -1;
		// TODO: need to delete from lmap_pid_reverse?
		at = atomic_dec_return(&lmap_active_threads);
	}

	return at;
}

int lmap_add_pid(int pid){
	unsigned h = hash_32(pid, lmap_PID_HASH_BITS);

	if(lmap_pid[h].pid == -1){
		lmap_pid[h].pid = pid;
		lmap_pid[h].tid = atomic_inc_return(&lmap_num_threads) - 1;
		atomic_inc_return(&lmap_active_threads);
		lmap_pid_reverse[lmap_pid[h].tid] = lmap_pid[h];

		/*if(do_tm)
			set_aff(pid, lmap_pid[h].tid);*/
		return lmap_pid[h].tid;
	}else
		printk("lmap BUG: XXX pid collision: %d->%d\n", lmap_pid[h].pid, lmap_pid[h].tid);
	
	return -1;
}

void lmap_pid_init(void){
	memset(lmap_pid, -1, sizeof(lmap_pid));
	memset(lmap_pid_reverse, -1, sizeof(lmap_pid_reverse));
	atomic_set(&lmap_num_threads, 0);
	atomic_set(&lmap_active_threads, 0);
}