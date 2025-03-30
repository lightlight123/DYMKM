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

#define OUTPUT_FILE_PATH "./memory_segment_hash.txt" // 输出文件路径

static int pid = -1;
module_param(pid, int, 0);
MODULE_PARM_DESC(pid, "Target process PID");

static struct file *file = NULL;

// 打开输出文件
static int open_output_file(void) {
    file = filp_open(OUTPUT_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (IS_ERR(file)) {
        pr_err("无法打开输出文件: %s\n", OUTPUT_FILE_PATH);
        return PTR_ERR(file);
    }
    pr_info("输出文件 %s 打开成功\n", OUTPUT_FILE_PATH);
    return 0;
}

// 写入数据到文件
static void write_to_file(const char *data) {
    if (file) {
        kernel_write(file, data, strlen(data), &file->f_pos);
    }
}

// 关闭文件
static void close_output_file(void) {
    if (file) {
        filp_close(file, NULL);
        file = NULL;
        pr_info("输出文件已关闭\n");
    }
}

// 检查页面是否已加载到物理内存中
static int is_page_in_memory(struct mm_struct *mm, unsigned long vaddr) {
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;

    pgd = pgd_offset(mm, vaddr);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) {
        return 0;
    }

    p4d = p4d_offset(pgd, vaddr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) {
        return 0;
    }

    pud = pud_offset(p4d, vaddr);
    if (pud_none(*pud) || pud_bad(*pud)) {
        return 0;
    }

    pmd = pmd_offset(pud, vaddr);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) {
        return 0;
    }

    pte = pte_offset_map(pmd, vaddr);
    if (!pte_present(*pte)) {
        pte_unmap(pte);
        return 0;
    }

    pte_unmap(pte);
    return 1;
}

// 生成页面的 SHA1 哈希值
static int generate_sha1_digest(const void *data, size_t len, unsigned char *digest) {
    struct crypto_shash *tfm;
    struct shash_desc *shash;
    int ret;

    tfm = crypto_alloc_shash("sha1", 0, 0);
    if (IS_ERR(tfm)) {
        pr_err("无法分配 SHA1 算法对象\n");
        return PTR_ERR(tfm);
    }

    shash = kmalloc(sizeof(*shash) + crypto_shash_descsize(tfm), GFP_KERNEL);
    if (!shash) {
        pr_err("无法分配哈希描述符\n");
        crypto_free_shash(tfm);
        return -ENOMEM;
    }

    shash->tfm = tfm;
    ret = crypto_shash_digest(shash, data, len, digest);

    kfree(shash);
    crypto_free_shash(tfm);
    return ret;
}

// 处理虚拟地址段
static int dump_segment_content(struct task_struct *task, unsigned long start, unsigned long end) {
    struct mm_struct *mm = task->mm;
    struct page *page;
    void *page_ptr;
    unsigned long vaddr;
    unsigned char digest[20];
    char output[256];
    int ret;

    for (vaddr = start; vaddr < end; vaddr += PAGE_SIZE) {
        if (!is_page_in_memory(mm, vaddr)) {
            pr_info("虚拟地址 0x%lx 未加载到物理内存中\n", vaddr);
            continue;
        }

        ret = get_user_pages_remote(mm, vaddr, 1, FOLL_FORCE, &page, NULL, NULL);
        if (ret <= 0 || !page) {
            pr_err("无法获取虚拟地址 0x%lx 对应的页面\n", vaddr);
            continue;
        }

        page_ptr = kmap(page);
        if (!page_ptr) {
            pr_err("无法映射虚拟地址 0x%lx 的页面\n", vaddr);
            put_page(page);
            continue;
        }

        ret = generate_sha1_digest(page_ptr, PAGE_SIZE, digest);
        if (ret == 0) {
            snprintf(output, sizeof(output), "vaddr: 0x%lx page_digest: ", vaddr);
            int i=0;
            for (i = 0; i < 20; i++) {
                snprintf(output + strlen(output), sizeof(output) - strlen(output), "%02x", digest[i]);
            }
            strncat(output, "\n", sizeof(output) - strlen(output));
            write_to_file(output);
        } else {
            pr_err("SHA1 生成失败，地址 0x%lx\n", vaddr);
        }

        kunmap(page);
        put_page(page);
    }

    return 0;
}

// 模块加载入口
static int __init memory_reader_init(void) {
    struct task_struct *task;
    struct mm_struct *mm;
    struct vm_area_struct *vma;
    unsigned long start_vaddr, end_vaddr;
    int ret;

    if (pid < 0) {
        pr_err("请提供有效的 PID\n");
        return -EINVAL;
    }

    ret = open_output_file();
    if (ret < 0) {
        return ret;
    }

    task = get_pid_task(find_get_pid(pid), PIDTYPE_PID);
    if (!task) {
        pr_err("未找到 PID 为 %d 的进程\n", pid);
        close_output_file();
        return -ESRCH;
    }

    mm = task->mm;
    if (!mm) {
        pr_err("无法获取 PID 为 %d 的 mm 结构\n", pid);
        close_output_file();
        return -EINVAL;
    }

    for (vma = mm->mmap; vma; vma = vma->vm_next) {
        if ((vma->vm_flags & VM_EXEC) && (vma->vm_flags & VM_READ) && !(vma->vm_flags & VM_WRITE)) {
            start_vaddr = vma->vm_start;
            end_vaddr = vma->vm_end;
            pr_info("找到 r-xp 段: 0x%lx - 0x%lx\n", start_vaddr, end_vaddr);
            dump_segment_content(task, start_vaddr, end_vaddr);
            break;
        }
    }

    close_output_file();
    return 0;
}

// 模块卸载入口
static void __exit memory_reader_exit(void) {
    pr_info("内核模块卸载完成\n");
}

module_init(memory_reader_init);
module_exit(memory_reader_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("读取指定进程第一个 r-xp 段的内容并保存虚拟地址与页面哈希到文本文件中");
