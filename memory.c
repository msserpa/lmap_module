#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/hashtable.h>
#include <linux/vmalloc.h>
#include <linux/migrate.h>

#include "eagermap/libmapping.h"

#define MAX_THREADS 4096
#define LMAP_MEM_HASH_BITS 26
#define LMAP_SHIFT 12

struct mem_s{
	unsigned long addr;
	s16 sharer[2];
	unsigned acc_n[8];
	u16 nmig;
};

extern int check_name(char *name);
extern int lmap_get_num_threads(void);
extern int lmap_get_tid(int pid);

int do_tm = 0;
int lmap_fac = 2;

struct mem_s *mem = NULL;
unsigned int matrix[MAX_THREADS * MAX_THREADS];

static inline struct mem_s* lmap_get_mem(unsigned long address){
	return &mem[hash_32(address, LMAP_MEM_HASH_BITS)];
}

static inline struct mem_s* lmap_get_mem_init(unsigned long address){
	struct mem_s *elem = lmap_get_mem(address);
	unsigned long page = address;

	if(elem->addr != page){ // new elem
		if(elem->addr)
			printk("lmap BUG: addr conflict: old: %lx  new:%lx\n", elem->addr, page);

		// elem->first_node = -1;
		// elem->cur_node = -1;

		elem->sharer[0] = -1;
		elem->sharer[1] = -1;
		memset(elem->acc_n, 0, sizeof(elem->acc_n));
		elem->nmig = 0;
		elem->addr = page;
	}

	return elem;
}

static inline int get_num_sharers(struct mem_s *elem){
	if(elem->sharer[0] == -1 && elem->sharer[1] == -1)
		return 0;

	if(elem->sharer[0] != -1 && elem->sharer[1] != -1)
		return 2;

	return 1;
}

static inline void inc_comm(int first, int second){
	static int max_threads_bits = ilog2(MAX_THREADS);

	if(first > second)
		matrix[(first << max_threads_bits) + second]++;
	else
		matrix[(second << max_threads_bits) + first]++;
}

static inline unsigned get_comm(int first, int second){
	static int max_threads_bits = ilog2(MAX_THREADS);

	if(first > second)
		return matrix[(first << max_threads_bits) + second];
	else
		return matrix[(second << max_threads_bits) + first];
}

void lmap_check_comm(int tid, unsigned long address){
	struct mem_s *elem = lmap_get_mem_init(address >> LMAP_SHIFT);
	int sh = get_num_sharers(elem);

	switch(sh){
		case 0: // no one accessed page before, store accessing thread in pos 0 
			elem->sharer[0] = tid;
		break;

		case 1: // one previous access => needs to be in pos 0 
			if(elem->sharer[0] != tid){
				inc_comm(tid, elem->sharer[0]);
				elem->sharer[1] = elem->sharer[0];
				elem->sharer[0] = tid;
			}
		break;

		case 2: // two previous accesses 
			if(elem->sharer[0] != tid && elem->sharer[1] != tid){
				inc_comm(tid, elem->sharer[0]);
				inc_comm(tid, elem->sharer[1]);
				elem->sharer[1] = elem->sharer[0];
				elem->sharer[0] = tid;
			}else if(elem->sharer[0] == tid){
				inc_comm(tid, elem->sharer[1]);
			}else if(elem->sharer[1] == tid){
				inc_comm(tid, elem->sharer[0]);
				elem->sharer[1] = elem->sharer[0];
				elem->sharer[0] = tid;
			}
		break;
	}
}

int lmap_check_dm(unsigned long address){
	int i, max = 0, max_old = 0, max_node = -1;
	struct mem_s *elem = lmap_get_mem_init(address);

	int my_node = cpu_to_node(raw_smp_processor_id());
	int nnodes = num_online_nodes();

	elem->acc_n[my_node]++;

	//printk("%lu: acc from node %d\n", address >> LMAP_SHIFT, my_node);

	for(i = 0; i < nnodes; i++){
		//printk("%d ", elem->acc_n[i]);
		if(elem->acc_n[i] > max){
			max_old = max;
			max = elem->acc_n[i];
			max_node = i;
		}
	}
	//printk(" max: %d max_old:%d\n", max, max_old);

	if(max > lmap_fac * (max_old + 1)){
		elem->nmig++;
		return max_node;
	}

	return -1;
}

void lmap_print_comm(void){
	int i, j;
	int nt = lmap_get_num_threads();
	unsigned long sum = 0, sum_sqr = 0;
	unsigned long av, va;

	if(nt < 2)
		return;

	for(i = nt - 1; i >= 0; i--){
		for(j = 0; j < nt; j++){
			int s = get_comm(i,j);
			sum += s;
			sum_sqr += s * s;
			printk("%u", s);
			if(j != nt - 1)
				printk(",");
		}
		printk("\n");
	}

	av = sum / nt / nt;
	va = (sum_sqr - ((sum * sum) / nt / nt)) / (nt - 1)/(nt - 1);

	printk("avg: %lu, var: %lu, hf: %lu\n", av, va, av ? va / av : 0);
}

static int pagestats_read(struct seq_file *m, void *v){
	long i, j, pagenr = 0, elements = 1UL << LMAP_MEM_HASH_BITS;
	int num_nodes = num_online_nodes();

	seq_printf(m, "nr\taddr\t");
	for(i = 0; i < num_nodes; i++)
		seq_printf(m, "\tN%ld", i);
	seq_printf(m, "\t#mig\n");
	// seq_printf(m, "\tcur\n");

	for(i = 0; i < elements; i++){
		if(mem[i].addr != 0){
			seq_printf(m, "%ld\t%10lx", pagenr++, mem[i].addr);
			for(j = 0; j < num_nodes; j++)
				seq_printf(m, "\t%u", mem[i].acc_n[j]);
			// seq_printf(m, "\tN%d\n", mem[i].cur_node);
			seq_printf(m, "\t%u\n", mem[i].nmig);
		}
	}

	return 0;
}

static ssize_t tm_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos){
	char buf[200];
	unsigned int v;

	copy_from_user(buf, buffer, count);
	buf[count-1] = 0;
	kstrtouint(buf, 0,  &v);

	printk("lmap: setting tm to %u\n", v);
	do_tm = v;

	return count;
}

static ssize_t fac_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos){
	char buf[200];
	unsigned int v;

	copy_from_user(buf, buffer, count);
	buf[count-1] = 0;
	kstrtouint(buf, 0,  &v);

	printk("lmap: setting local_fac to %u\n", v);
	lmap_fac = v;

	return count;
}

static int tm_read(struct seq_file *m, void *v){
	seq_printf(m, "TM: %d\n", do_tm);
	return 0;
}

static int fac_read(struct seq_file *m, void *v){
	seq_printf(m, "Local fac: %d\n", lmap_fac);
	return 0;
}

static int matrix_read(struct seq_file *m, void *v){
	int i, j;
	int nt = lmap_get_num_threads();
	unsigned long sum = 0, sum_sqr = 0;
	unsigned long av, va;

	if(nt < 2)
		return 0;

	for(i = nt - 1; i >= 0; i--){
		for(j = 0; j < nt; j++){
			int s = get_comm(i, j);
			sum += s;
			sum_sqr += s * s;
			seq_printf(m, "%u", s);
			if(j != nt - 1)
				seq_printf(m, ",");
		}
		seq_printf(m, "\n");
	}

	av = sum / nt / nt;
	va = (sum_sqr - ((sum * sum) / nt / nt)) / (nt - 1) / (nt - 1);

	seq_printf(m, "avg: %lu, var: %lu, hf: %lu\n", av, va, av ? va/av : 0);
	return 0;
}

static int pagestats_open(struct inode *inode, struct file *file){
	return single_open(file, pagestats_read, NULL);
}

static int tm_open(struct inode *inode, struct file *file){
	return single_open(file, tm_read, NULL);
}

static int fac_open(struct inode *inode, struct file *file){
	return single_open(file, fac_read, NULL);
}

static int matrix_open(struct inode *inode, struct file *file){
	return single_open(file, matrix_read, NULL);
}

static const struct file_operations pagestats_ops ={
	.owner = THIS_MODULE,
	.open = pagestats_open,
	.read = seq_read,
	.llseek	= seq_lseek,
	.release = single_release,
};

static const struct file_operations tm_ops ={
	.owner = THIS_MODULE,
	.open = tm_open,
	.read = seq_read,
	.llseek	= seq_lseek,
	.write = tm_write,
	.release = single_release,
};

static const struct file_operations fac_ops ={
	.owner = THIS_MODULE,
	.open = fac_open,
	.read = seq_read,
	.llseek	= seq_lseek,
	.write = fac_write,
	.release = single_release,
};

static const struct file_operations matrix_ops ={
	.owner = THIS_MODULE,
	.open = matrix_open,
	.read = seq_read,
	.llseek	= seq_lseek,
	.release = single_release,
};


void lmap_mem_init(void){
	/* static struct proc_dir_entry *lmap_proc_root = NULL; */
	if(!mem)
		mem = vmalloc(sizeof(struct mem_s) *(1 << LMAP_MEM_HASH_BITS));

	if(!mem)
		printk("lmap BUG, no mem");
	else
		memset(mem, 0, sizeof(struct mem_s) *(1 << LMAP_MEM_HASH_BITS));

	memset(matrix, 0, sizeof(matrix));

	/* if(!lmap_proc_root){
		lmap_proc_root = proc_mkdir("lmap", NULL);
		proc_create("pagestats", 0666, lmap_proc_root, &pagestats_ops);
		proc_create("tm", 0666, lmap_proc_root, &tm_ops);
		proc_create("matrix", 0666, lmap_proc_root, &matrix_ops);
		proc_create("fac", 0666, lmap_proc_root, &fac_ops);
	} */
}

extern struct page *vm_normal_page(struct vm_area_struct *vma, unsigned long addr, pte_t pte);
extern void task_numa_fault(int last_cpupid, int mem_node, int pages, int flags);

int(*original_numa_migrate_prep)(struct page *page, struct vm_area_struct *vma, unsigned long addr, int page_nid, int *flags);
extern int(*numa_migrate_prep)(struct page *page, struct vm_area_struct *vma, unsigned long addr, int page_nid, int *flags);

int lmap_numa_migrate_prep(struct page *page, struct vm_area_struct *vma, unsigned long addr, int page_nid, int *flags){
	get_page(page);

	count_vm_numa_event(NUMA_HINT_FAULTS);
	if(page_nid == numa_node_id()){
		count_vm_numa_event(NUMA_HINT_FAULTS_LOCAL);
		*flags |= TNF_FAULT_LOCAL;
	}

	if(lmap_get_tid(current->pid) == -1)
		return -1; // return mpol_misplaced(page, vma, addr);
	else{
		int t = lmap_check_dm(page_to_pfn(page));
		//printk("lmap: page from %d to %d\n", page_nid, t);
		return t == page_nid ? -1 : t;
	}

	return -1; //return mpol_misplaced(page, vma, addr);
}

int(*original_do_numa_page)(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, pte_t pte, pte_t *ptep, pmd_t *pmd);
extern int(*do_numa_page)(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, pte_t pte, pte_t *ptep, pmd_t *pmd);

int lmap_do_numa_page(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, pte_t pte, pte_t *ptep, pmd_t *pmd){
	struct page *page = NULL;
	spinlock_t *ptl;
	int page_nid = -1;
	int last_cpupid;
	int target_nid;
	bool migrated = false;
	bool was_writable = pte_write(pte);
	int flags = 0;

	/* A PROT_NONE fault should not end up here */
	BUG_ON(!(vma->vm_flags &(VM_READ | VM_EXEC | VM_WRITE)));

	/*
	* The "pte" at this point cannot be used safely without
	* validation through pte_unmap_same(). It's of NUMA type but
	* the pfn may be screwed if the read is non atomic.
	*
	* We can safely just do a "set_pte_at()", because the old
	* page table entry is not accessible, so there would be no
	* concurrent hardware modifications to the PTE.
	*/
	ptl = pte_lockptr(mm, pmd);
	spin_lock(ptl);
	if(unlikely(!pte_same(*ptep, pte))){
		pte_unmap_unlock(ptep, ptl);
		goto out;
	}

	/* Make it present again */
	pte = pte_modify(pte, vma->vm_page_prot);
	pte = pte_mkyoung(pte);
	if(was_writable)
		pte = pte_mkwrite(pte);
	set_pte_at(mm, addr, ptep, pte);
	update_mmu_cache(vma, addr, ptep);

	page = vm_normal_page(vma, addr, pte);
	if(!page){
		pte_unmap_unlock(ptep, ptl);
		return 0;
	}

	/*
	* Avoid grouping on RO pages in general. RO pages shouldn't hurt as
	* much anyway since they can be in shared cache state. This misses
	* the case where a mapping is writable but the process never writes
	* to it but pte_write gets cleared during protection updates and
	* pte_dirty has unpredictable behaviour between PTE scan updates,
	* background writeback, dirty balancing and application behaviour.
	*/
	if(!(vma->vm_flags & VM_WRITE))
		flags |= TNF_NO_GROUP;

	/*
	* Flag if the page is shared between multiple address spaces. This
	* is later used when determining whether to group tasks together
	*/
	if(page_mapcount(page) > 1 && (vma->vm_flags & VM_SHARED))
		flags |= TNF_SHARED;

	last_cpupid = page_cpupid_last(page);
	page_nid = page_to_nid(page);
	target_nid = numa_migrate_prep(page, vma, addr, page_nid, &flags);
	pte_unmap_unlock(ptep, ptl);
	if(target_nid == -1){
		put_page(page);
		goto out;
	}

	/* Migrate to the requested node */
	migrated = migrate_misplaced_page(page, vma, target_nid);
	if(check_name(current->comm))
		printk("lmap: page %p was %smigrate from %d to %d\n", page, migrated == 0 ? "not " : "", page_nid, target_nid);
	if(migrated){
		page_nid = target_nid;
		flags |= TNF_MIGRATED;
	}else
		flags |= TNF_MIGRATE_FAIL;

out:
	if(page_nid != -1)
		task_numa_fault(last_cpupid, page_nid, 1, flags);

	return 0;
}

extern int do_anonymous_page(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long address, pte_t *page_table, pmd_t *pmd, unsigned int flags);
extern int do_fault(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long address, pte_t *page_table, pmd_t *pmd, unsigned int flags, pte_t orig_pte);
extern int do_wp_page(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long address, pte_t *page_table, pmd_t *pmd, spinlock_t *ptl, pte_t orig_pte) __releases(ptl);
extern int do_swap_page(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long address, pte_t *page_table, pmd_t *pmd, unsigned int flags, pte_t orig_pte);

int(*original_handle_pte_fault)(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long address, pte_t *pte, pmd_t *pmd, unsigned int flags);
extern int(*handle_pte_fault)(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long address, pte_t *pte, pmd_t *pmd, unsigned int flags);

int lmap_handle_pte_fault(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long address, pte_t *pte, pmd_t *pmd, unsigned int flags){
	pte_t entry;
	spinlock_t *ptl;
	int tid = lmap_get_tid(current->pid);

	if(tid > -1)
		lmap_check_comm(tid, address);

	/*
	* some architectures can have larger ptes than wordsize,
	* e.g.ppc44x-defconfig has CONFIG_PTE_64BIT=y and CONFIG_32BIT=y,
	* so READ_ONCE or ACCESS_ONCE cannot guarantee atomic accesses.
	* The code below just needs a consistent view for the ifs and
	* we later double check anyway with the ptl lock held. So here
	* a barrier will do.
	*/
	entry = *pte;
	barrier();
	if(!pte_present(entry)){
		if(pte_none(entry)){
			if(vma_is_anonymous(vma))
				return do_anonymous_page(mm, vma, address, pte, pmd, flags);
			else
				return do_fault(mm, vma, address, pte, pmd, flags, entry);
		}
		return do_swap_page(mm, vma, address, pte, pmd, flags, entry);
	}

	if(pte_protnone(entry))
		return do_numa_page(mm, vma, address, entry, pte, pmd);

	ptl = pte_lockptr(mm, pmd);
	spin_lock(ptl);
	if(unlikely(!pte_same(*pte, entry)))
		goto unlock;
	if(flags & FAULT_FLAG_WRITE){
		if(!pte_write(entry))
			return do_wp_page(mm, vma, address, pte, pmd, ptl, entry);
		entry = pte_mkdirty(entry);
	}
	entry = pte_mkyoung(entry);
	if(ptep_set_access_flags(vma, address, pte, entry, flags & FAULT_FLAG_WRITE)){
		update_mmu_cache(vma, address, pte);
	}else{
		/*
		* This is needed only for protection faults but the arch code
		* is not yet telling us if this is a protection fault or not.
		* This still avoids useless tlb flushes for .text page faults
		* with threads.
		*/
		if(flags & FAULT_FLAG_WRITE)
			flush_tlb_fix_spurious_fault(vma, address);
	}
unlock:
	pte_unmap_unlock(pte, ptl);
	
	return 0;
}