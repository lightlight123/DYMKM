#ifndef AGENT_H
#define AGENT_H

#include <stdint.h>
#include <semaphore.h>

#define MAX_BATCH_SIZE 8
#define SHM_NAME "/cf_shm"
#define SHM_SIZE (sizeof(struct shm_control) + MAX_BATCH_SIZE * sizeof(struct controlflow_batch))

// 显式声明C语言符号导出
#ifdef __cplusplus
extern "C" {
#endif

// 控制流信息结构体（强制8字节对齐）
struct controlflow_info {
    uint64_t source_id;          // 基本块ID
    uint64_t addrto_offset;      // 目标地址偏移
} __attribute__((aligned(8)));

// 批量控制流信息结构体
struct controlflow_batch {
    uint64_t batch_size;         
    struct controlflow_info data[MAX_BATCH_SIZE];
} __attribute__((aligned(8)));

// 共享内存控制块（包含进程间信号量）
struct shm_control {
    sem_t lock;          // 互斥信号量
    sem_t data_ready;    // 数据通知信号量
    uint32_t head;
    uint32_t tail;
    uint32_t buffer_size;
};

// 共享内存上下文
struct shared_mem_ctx {
    int is_creator;              // 是否为创建者
    struct shm_control *ctrl;    // 控制块指针
    struct controlflow_batch *data_area; // 数据区指针
};

__attribute__((visibility("default"))) 
void add_controlflow_entry(uint64_t source_bbid, uint64_t src_module_base, uint64_t target_offset);

__attribute__((visibility("default")))
struct shared_mem_ctx *init_shared_mem(int is_creator);

void read_controlflow_data(struct shared_mem_ctx *ctx);
void cleanup_shared_mem(struct shared_mem_ctx *ctx);

#ifdef __cplusplus
}
#endif
#endif