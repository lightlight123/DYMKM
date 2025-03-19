#ifndef AGENT_H
#define AGENT_H

#include <stdint.h>
#include <stdatomic.h>

#define MAX_BATCH_SIZE 8

// 控制流信息结构体
struct controlflow_info {
    uint64_t source_id;          // 基本块ID
    uint64_t addrto_offset;      // 目标地址偏移
};

// 批量控制流信息结构体
struct controlflow_batch {
    uint64_t batch_size;         // 不超过MAX_BATCH_SIZE
    struct controlflow_info data[MAX_BATCH_SIZE]; // 控制流数据
};

// 队列控制块
struct shm_control {
    _Atomic(uint32_t) head;      // 队列头指针
    _Atomic(uint32_t) tail;      // 队列尾指针
    uint32_t buffer_size;        // 队列容量 (最大值为 MAX_BATCH_SIZE)
    _Atomic(uint32_t) lock;      // 自旋锁，防止多线程并发访问
    _Atomic(uint32_t) new_message; // 新消息标志
};

// 共享内存上下文
struct shared_mem_ctx {
    void *shm_base;              // 共享内存基地址
    struct shm_control *ctrl;    // 队列控制块
    struct controlflow_batch *data_area; // 数据存储区（修正为批量结构）
};

// 共享内存操作函数
void add_controlflow_entry(uint64_t source_id, uint64_t offset);
struct shared_mem_ctx *init_shared_mem();
void write_controlflow_data(struct shared_mem_ctx *ctx, struct controlflow_batch *batch);
void cleanup_shared_mem(struct shared_mem_ctx *ctx);

#endif // AGENT_H