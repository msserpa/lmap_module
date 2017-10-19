#include <linux/module.h>
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

extern void lmap_probes_init(void);
extern void lmap_probes_cleanup(void);

extern int (*original_do_numa_page)(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, pte_t pte, pte_t *ptep, pmd_t *pmd);
extern int (*do_numa_page)(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, pte_t pte, pte_t *ptep, pmd_t *pmd);
extern int lu_do_numa_page(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long addr, pte_t pte, pte_t *ptep, pmd_t *pmd);

extern int (*original_numa_migrate_prep)(struct page *page, struct vm_area_struct *vma, unsigned long addr, int page_nid, int *flags);
extern int (*numa_migrate_prep)(struct page *page, struct vm_area_struct *vma, unsigned long addr, int page_nid, int *flags);
extern int lu_numa_migrate_prep(struct page *page, struct vm_area_struct *vma, unsigned long addr, int page_nid, int *flags);

extern int (*original_migrate_misplaced_page)(struct page *page, struct vm_area_struct *vma, int node);
//extern int (*migrate_misplaced_page)(struct page *page, struct vm_area_struct *vma, int node);
extern int lu_migrate_misplaced_page(struct page *page, struct vm_area_struct *vma, int node);

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