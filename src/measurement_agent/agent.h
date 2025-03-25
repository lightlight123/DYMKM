// agent.h
#ifndef AGENT_H
#define AGENT_H

#include <stdint.h>
#include <stdatomic.h>

#define MAX_BATCH_SIZE 7
#define SHM_NAME "/cf_shm"
#define SHM_SIZE (sizeof(struct shm_control) + MAX_BATCH_SIZE * sizeof(struct controlflow_batch))

#ifdef __cplusplus
extern "C" {
#endif

// 控制流信息结构体（强制8字节对齐）
struct controlflow_info {
    uint64_t source_id;
    uint64_t addrto_offset;
} __attribute__((aligned(8)));

// 批量控制流信息结构体
struct controlflow_batch {
    uint64_t batch_size;
    struct controlflow_info data[MAX_BATCH_SIZE];
} __attribute__((aligned(8)));

// 共享内存控制块（原子操作实现）
struct shm_control {
    atomic_uint head;            // 原子头指针
    atomic_uint tail;            // 原子尾指针
    atomic_uint buffer_size;     // 原子缓冲区大小
    atomic_uint data_count;      // 原子数据计数器
    atomic_flag lock;            // 自旋锁标志
};

// 共享内存上下文
struct shared_mem_ctx {
    int is_creator;
    struct shm_control *ctrl;
    struct controlflow_batch *data_area;
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