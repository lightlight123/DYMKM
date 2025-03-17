#ifndef __SHARED_MEM_TA_H__
#define __SHARED_MEM_TA_H__
#include <stdatomic.h>

/* UUID of the Shared Memory Trusted Application */
//86bb09d5-819a-461c-bd7e-972129604e0c
#define TA_SHARED_MEM_UUID \
	{ 0x86bb09d5, 0x819a, 0x461c, \
		{ 0xbd, 0x7e, 0x97, 0x21, 0x29, 0x60, 0x4e, 0x0c } }

#define TA_CMD_ENQUEUE 0
#define TA_CMD_PROCESS 1

#define MAX_BATCH_SIZE 8
#define TEE_HASH_SHA256_SIZE 32

struct controlflow_info {
    uint64_t source_id;
    uint64_t addrto_offset;
    uint8_t hash[TEE_HASH_SHA256_SIZE];
};

struct controlflow_batch {
    uint64_t batch_size;
    struct controlflow_info data[];
};

struct shm_control {
    _Atomic(uint32_t) head;
    _Atomic(uint32_t) tail;
    uint32_t buffer_size;
    _Atomic(uint32_t) lock;     
    _Atomic(uint32_t) new_message; //新消息标志
    _Atomic(uint32_t) verify_ok;   //验证成功标志
};

// 新增基线控制块
struct hash_baseline {
    uint8_t initial_hash[TEE_HASH_SHA256_SIZE]; // 链式哈希初始值
    uint32_t generation;                       // 基线版本号
    atomic_int locked;                          // 基线更新锁
};

// 共享内存上下文结构
struct shared_mem_ctx {
    void *shm_base;                  // 共享内存基地址
    struct shm_control *ctrl;        // 队列控制块
    struct hash_baseline *baseline;  // 哈希基线块 
    struct controlflow_info *data_area; // 数据存储区
};

#endif /* __SHARED_MEM_TA_H__ */