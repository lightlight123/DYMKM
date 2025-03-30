#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/highmem.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/mm_types.h>
#include <linux/pgtable.h>
#include <crypto/hash.h>

#define SHA1_DIGEST_SIZE 20

static int pid = -1;
module_param(pid, int, 0);
MODULE_PARM_DESC(pid, "Target process PID");

// ���ҳ���Ƿ��Ѽ��ص������ڴ���
static int is_page_in_memory(struct mm_struct* mm, unsigned long vaddr) {
    pgd_t* pgd;
    p4d_t* p4d;
    pud_t* pud;
    pmd_t* pmd;
    pte_t* pte;

    pgd = pgd_offset(mm, vaddr);
    if (pgd_none(*pgd) || pgd_bad(*pgd))
        return 0;

    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || p4d_bad(*p4d))
        return 0;

    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud) || pud_bad(*pud))
        return 0;

    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd) || pmd_bad(*pmd))
        return 0;

    pte = pte_offset_map(pmd, vaddr);
    if (!pte_present(*pte)) {
        pte_unmap(pte);
        return 0;
    }

    pte_unmap(pte);
    return 1;
}

static int hash_page_content(void* page_ptr, char* digest) {
    struct crypto_shash* tfm;
    struct shash_desc* shash;
    int ret;

    tfm = crypto_alloc_shash("sha1", 0, 0);
    if (IS_ERR(tfm)) {
        pr_err("�޷����� SHA1 ��ϣ���\n");
        return PTR_ERR(tfm);
    }

    shash = kzalloc(sizeof(*shash) + crypto_shash_descsize(tfm), GFP_KERNEL);
    if (!shash) {
        crypto_free_shash(tfm);
        pr_err("�޷����� shash_desc\n");
        return -ENOMEM;
    }

    shash->tfm = tfm;

    ret = crypto_shash_init(shash);
    if (ret) {
        pr_err("SHA1 ��ʼ��ʧ��\n");
        goto out;
    }

    ret = crypto_shash_update(shash, page_ptr, PAGE_SIZE);
    if (ret) {
        pr_err("SHA1 ����ʧ��\n");
        goto out;
    }

    ret = crypto_shash_final(shash, digest);
    if (ret)
        pr_err("SHA1 ���ջ�ʧ��\n");

out:
    kfree(shash);
    crypto_free_shash(tfm);
    return ret;
}

static int dump_segment_content(struct task_struct* task, unsigned long start, unsigned long end) {
    struct mm_struct* mm = task->mm;
    struct page* page;
    void* page_ptr;
    unsigned long vaddr;
    char digest[SHA1_DIGEST_SIZE];
    int ret;

    for (vaddr = start; vaddr < end; vaddr += PAGE_SIZE) {
        if (!is_page_in_memory(mm, vaddr)) {
            pr_info("�����ַ 0x%lx δ���ص������ڴ���\n", vaddr);
            continue;
        }

        ret = get_user_pages_remote(mm, vaddr, 1, FOLL_FORCE, &page, NULL, NULL);
        if (ret <= 0 || !page) {
            pr_err("�޷���ȡ�����ַ 0x%lx ��Ӧ��ҳ��\n", vaddr);
            continue;
        }

        page_ptr = kmap(page);
        if (!page_ptr) {
            pr_err("�޷�ӳ�������ַ 0x%lx ��ҳ��\n", vaddr);
            put_page(page);
            continue;
        }

        ret = hash_page_content(page_ptr, digest);
        if (ret == 0) {
            pr_info("vaddr: 0x%lx, page_digest: ", vaddr);
            int i;
            for (i = 0; i < SHA1_DIGEST_SIZE; i++)
                pr_cont("%02x", (unsigned char)digest[i]);
            pr_cont("\n");
        }

        kunmap(page);
        put_page(page);
    }

    return 0;
}

static int __init memory_reader_init(void) {
    struct task_struct* task;
    struct mm_struct* mm;
    struct vm_area_struct* vma;
    unsigned long start_vaddr, end_vaddr;

    if (pid < 0) {
        pr_err("���ṩ��Ч�� PID\n");
        return -EINVAL;
    }

    pr_info("������ PID: %d\n", pid);

    task = get_pid_task(find_get_pid(pid), PIDTYPE_PID);
    if (!task) {
        pr_err("δ�ҵ� PID Ϊ %d �Ľ���\n", pid);
        return -ESRCH;
    }

    mm = task->mm;
    if (!mm) {
        pr_err("�޷���ȡ PID Ϊ %d �� mm �ṹ\n", pid);
        return -EINVAL;
    }

    for (vma = mm->mmap; vma; vma = vma->vm_next) {
        if ((vma->vm_flags & VM_EXEC) && (vma->vm_flags & VM_READ) && !(vma->vm_flags & VM_WRITE)) {
            start_vaddr = vma->vm_start;
            end_vaddr = vma->vm_end;
            pr_info("�ҵ� r-xp ��: 0x%lx - 0x%lx\n", start_vaddr, end_vaddr);
            dump_segment_content(task, start_vaddr, end_vaddr);
            break;
        }
    }

    pr_info("��ɶ� PID %d �������ַ�ռ����\n", pid);
    return 0;
}

static void __exit memory_reader_exit(void) {
    pr_info("�ں�ģ��ж�����\n");
}

module_init(memory_reader_init);
module_exit(memory_reader_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("��ָ�����̵��ڴ�ҳ���ݽ��� SHA-1 ��ϣ���㲢��ӡժҪ");