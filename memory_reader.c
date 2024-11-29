#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/highmem.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/mm_types.h>
#include <linux/pgtable.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/file.h>

#define PAGE_SIZE 4096
#define OUTPUT_FILE_PATH "./memory_fisco.txt" // 输出文件路径

static int pid = -1;
static unsigned long start_vaddr = 0;
static unsigned long end_vaddr = 0;
module_param(pid, int, 0);
module_param(start_vaddr, ulong, 0);
module_param(end_vaddr, ulong, 0);
MODULE_PARM_DESC(pid, "Target process PID");
MODULE_PARM_DESC(start_vaddr, "Start virtual address");
MODULE_PARM_DESC(end_vaddr, "End virtual address");

static struct file *file = NULL;

// 打开输出文件
static int open_output_file(void) {
    file = filp_open(OUTPUT_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (IS_ERR(file)) {
        pr_err("无法打开输出文件: %s\n", OUTPUT_FILE_PATH);
        return PTR_ERR(file);
    }
    return 0;
}

// 写入数据到文件
static void write_to_file(const char *data, size_t size) {
    if (file) {
        kernel_write(file, data, size, &file->f_pos);
    }
}

// 关闭文件
static void close_output_file(void) {
    if (file) {
        filp_close(file, NULL);
        file = NULL;
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

static int read_physical_page(struct task_struct *task, unsigned long vaddr) {
    struct mm_struct *mm = task->mm;
    struct page *page;
    void *page_ptr;
    unsigned long offset, paddr;
    int ret;
    char *buffer;
    int i;

    if (!is_page_in_memory(mm, vaddr)) {
        pr_info("虚拟地址 0x%lx 未加载到物理内存中\n", vaddr);
        return -EINVAL;
    }

    ret = get_user_pages_remote(mm, vaddr, 1, FOLL_FORCE, &page, NULL, NULL);
    if (ret <= 0 || !page) {
        pr_err("无法获取虚拟地址 0x%lx 对应的页面\n", vaddr);
        return -EINVAL;
    }

    page_ptr = kmap(page);
    if (!page_ptr) {
        pr_err("无法映射页面内容\n");
        put_page(page);
        return -ENOMEM;
    }

    offset = vaddr & (PAGE_SIZE - 1);
    paddr = page_to_pfn(page) * PAGE_SIZE + offset;

    buffer = kmalloc(PAGE_SIZE * 3 + 100, GFP_KERNEL); // 为输出内容分配内存
    if (!buffer) {
        kunmap(page);
        put_page(page);
        return -ENOMEM;
    }

    // 写入虚拟地址和物理地址信息
    snprintf(buffer, PAGE_SIZE * 3 + 100, "虚拟地址: 0x%lx, 物理地址: 0x%lx\n页面内容:\n", vaddr, paddr);
    for (i = 0; i < PAGE_SIZE; i++) {
        if (i % 16 == 0) {
            strcat(buffer, "\n");
        }
        sprintf(buffer + strlen(buffer), "%02x ", *((unsigned char *)(page_ptr + i)));
    }
    strcat(buffer, "\n");

    // 写入到文件
    write_to_file(buffer, strlen(buffer));

    kunmap(page);
    put_page(page);
    kfree(buffer);
    return 0;
}

static int __init memory_reader_init(void) {
    struct task_struct *task;
    int ret;

    if (pid < 0 || start_vaddr == 0 || end_vaddr == 0 || start_vaddr >= end_vaddr) {
        pr_err("请提供有效的 PID 和虚拟地址范围\n");
        return -EINVAL;
    }

    pr_info("检查进程 PID: %d 的虚拟地址范围 0x%lx - 0x%lx\n", pid, start_vaddr, end_vaddr);

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

    unsigned long vaddr;
    for (vaddr = start_vaddr; vaddr < end_vaddr; vaddr += PAGE_SIZE) {
        read_physical_page(task, vaddr);
    }

    close_output_file();
    return 0;
}

static void __exit memory_reader_exit(void) {
    pr_info("内核模块卸载完成\n");
}

module_init(memory_reader_init);
module_exit(memory_reader_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("读取指定进程虚拟地址范围的物理页面内容并保存到文件");
