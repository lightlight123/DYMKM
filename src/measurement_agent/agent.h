#ifndef AGENT_H
#define AGENT_H

#include <stdint.h>
#include <stdatomic.h>
#include <semaphore.h> 


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
    sem_t lock;          // 替换原子锁为信号量
    uint32_t head;       
    uint32_t tail;
    uint32_t buffer_size;
    sem_t new_message;   // 替换原子标志
};

// 共享内存上下文
struct shared_mem_ctx {
    void *shm_base;              // 共享内存基地址
    struct shm_control *ctrl;    // 队列控制块
    struct controlflow_batch *data_area; 
    sem_t *sem_lock;             // 互斥信号量 
    sem_t *sem_data;             // 数据通知信号量
};

// 共享内存操作函数
struct shared_mem_ctx *init_shared_mem(int is_creator);
void write_controlflow_data(struct shared_mem_ctx *ctx, struct controlflow_batch *batch);
void read_controlflow_data(struct shared_mem_ctx *ctx);
void cleanup_shared_mem(struct shared_mem_ctx *ctx);

#endif // AGENT_H