#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/migrate.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matheus Serpa");
MODULE_DESCRIPTION("automatic thread and data mapping");

inline int check_name(char *name) {
	int len = strlen(name);

	/* Only programs whose name ends with ".x" are accepted */
	if (name[len-2] == '.' && name[len-1] == 'x')
		return 1;

	return 0;
}

extern int (*original_do_numa_page)(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, pte_t pte, pte_t *ptep, pmd_t *pmd);
extern int (*do_numa_page)(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, pte_t pte, pte_t *ptep, pmd_t *pmd);
extern int lu_do_numa_page(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, pte_t pte, pte_t *ptep, pmd_t *pmd);

extern int (*original_numa_migrate_prep)(struct page *page, struct vm_area_struct *vma, unsigned long addr, int page_nid, int *flags);
extern int (*numa_migrate_prep)(struct page *page, struct vm_area_struct *vma, unsigned long addr, int page_nid, int *flags);
extern int lu_numa_migrate_prep(struct page *page, struct vm_area_struct *vma, unsigned long addr, int page_nid, int *flags);

extern int (*original_migrate_misplaced_page)(struct page *page, struct vm_area_struct *vma, int node);
//extern int (*migrate_misplaced_page)(struct page *page, struct vm_area_struct *vma, int node);
extern int lu_migrate_misplaced_page(struct page *page, struct vm_area_struct *vma, int node);

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


static void lmap_probes_init(void) {
	int ret;

	if ((ret=register_jprobe(&process_probe))) {
		printk("lmap bug: acct_update_integrals missing, %d\n", ret);
	}
	if ((ret=register_kretprobe(&thread_probe))) {
		printk("lmap bug: _do_fork missing, %d\n", ret);
	}
}

static void lmap_probes_cleanup(void) {
	unregister_jprobe(&process_probe);
	unregister_kretprobe(&thread_probe);
}

int init_module(void) {
	printk(KERN_INFO "lmap: starting...\n");

	lmap_probes_init();

	original_do_numa_page = do_numa_page;
	do_numa_page = &lu_do_numa_page;

	original_numa_migrate_prep = numa_migrate_prep;
	numa_migrate_prep = &lu_numa_migrate_prep;

	original_migrate_misplaced_page = migrate_misplaced_page;
	migrate_misplaced_page = &lu_migrate_misplaced_page;

	printk(KERN_INFO "lamp: started!\n");

	return 0;
}

void cleanup_module(void){
	printk(KERN_INFO "lmap: exiting...\n");

	lmap_probes_cleanup();

	do_numa_page = original_do_numa_page;
	numa_migrate_prep = original_numa_migrate_prep;
	migrate_misplaced_page = original_migrate_misplaced_page;

	printk(KERN_INFO "lmap: exit!\n");
}