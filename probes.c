#include <linux/kprobes.h>
#include <linux/kthread.h>

extern int lmap_get_tid(int pid);
extern int lmap_delete_pid(int pid);
extern int lmap_get_active_threads(void);
extern void lmap_pid_init(void);
extern int lmap_add_pid(int pid);

extern void lmap_mem_init(void);

extern int check_name(char *name);

static void process_handler(struct task_struct *tsk){
	int tid = lmap_get_tid(tsk->pid);

	// kernel/exit.c
	if(tid > -1 && (tsk->flags & PF_EXITING)){
		int at = lmap_delete_pid(tsk->pid);
		printk("lmap : %s stop (pid %d, tid %d), #active: %d\n", tsk->comm, tsk->pid, tid, at);
		if(at == 0){
			printk("lmap : stop app %s (pid %d, tid %d)\n", tsk->comm, tsk->pid, tid);
			// lmap_print_comm();
			// print_stats();
			// reset_stats();
		}
		jprobe_return();
	}

	// fs/exec.c
	if(check_name(tsk->comm) && tid == -1 && !(tsk->flags & PF_EXITING)){
		if(lmap_get_active_threads() == 0) {
			lmap_pid_init();
			tid = lmap_add_pid(tsk->pid);
			printk("lmap : new process %s (pid %d, tid %d); #active: %d\n", tsk->comm, tsk->pid, tid, lmap_get_active_threads());
			lmap_mem_init();
			//if(!lmap_map_thread)
				//lmap_map_thread = kthread_run(lmap_map_func, NULL, "lmap_map_thread");
		}else{
			tid = lmap_add_pid(tsk->pid);
			printk("lmap : new process %s (pid %d, tid %d); #active: %d\n", tsk->comm, tsk->pid, tid, lmap_get_active_threads());
		}
	}

	jprobe_return();
}

 // kernel/fork.c
static int thread_handler(struct kretprobe_instance *ri, struct pt_regs *regs){
	int pid = regs_return_value(regs);

	if(check_name(current->comm) && pid > 0){
		int tid = lmap_add_pid(pid);
		printk("lmap: new thread %s (pid:%d, tid:%d); #active: %d\n", current->comm, pid, tid, lmap_get_active_threads());
	}

	return 0;
}


static struct kretprobe thread_probe = {
	.handler = thread_handler,
	.kp.symbol_name = "_do_fork",
};

static struct jprobe process_probe = {
	.entry = process_handler,
	.kp.symbol_name = "acct_update_integrals",
};


void lmap_probes_init(void){
	int ret;

	if((ret = register_jprobe(&process_probe))){
		printk("lmap bug: acct_update_integrals missing, %d\n", ret);
	}

	if((ret = register_kretprobe(&thread_probe))){
		printk("lmap bug: _do_fork missing, %d\n", ret);
	}
}

void lmap_probes_cleanup(void){
	unregister_jprobe(&process_probe);
	unregister_kretprobe(&thread_probe);
}