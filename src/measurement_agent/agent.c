#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

#define MAX_BATCH_SIZE 8
#define TEE_HASH_SHA256_SIZE 32

// 控制流信息结构体
struct controlflow_info {
    uint64_t source_id;          // 基本块ID
    uint64_t addrto_offset;      // 目标地址偏移
    uint8_t hash[TEE_HASH_SHA256_SIZE]; // 哈希值
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
    _Atomic(uint32_t) verify_ok;  // 度量数据是否成功验证的标志
};

// 基线控制块
struct hash_baseline {
    uint8_t initial_hash[TEE_HASH_SHA256_SIZE]; // 链式哈希初始值
    uint32_t generation;                        // 基线版本号
    _Atomic(uint32_t) locked;                   // 基线更新锁
};

// 共享内存上下文
struct shared_mem_ctx {
    void *shm_base;                     // 共享内存基地址
    struct shm_control *ctrl;           // 队列控制块
    struct hash_baseline *baseline;     // 哈希基线块 
    struct controlflow_info *data_area; // 数据存储区
};

// 共享内存操作函数
void write_controlflow_data(struct shared_mem_ctx *ctx, struct controlflow_batch *batch) {
    // 获取控制块
    struct shm_control *ctrl = ctx->ctrl;
    
    // 加锁
    while (atomic_exchange(&ctrl->lock, 1)) {
        // 自旋等待锁
    }

    // 检查队列是否有足够空间
    uint32_t next_tail = (atomic_load(&ctrl->tail) + 1) % MAX_BATCH_SIZE;
    if (next_tail == atomic_load(&ctrl->head)) {
        // 队列已满，等待处理
        atomic_store(&ctrl->new_message, 0);
    } else {
        // 写入数据
        memcpy(&ctx->data_area[atomic_load(&ctrl->tail)], batch, sizeof(*batch));
        atomic_store(&ctrl->tail, next_tail);
        atomic_store(&ctrl->new_message, 1);
    }

    // 解锁
    atomic_store(&ctrl->lock, 0);
}

void read_controlflow_data(struct shared_mem_ctx *ctx) {
    // 获取控制块
    struct shm_control *ctrl = ctx->ctrl;
    
    // 加锁
    while (atomic_exchange(&ctrl->lock, 1)) {
        // 自旋等待锁
    }

    if (atomic_load(&ctrl->new_message)) {
        // 从队列读取数据
        uint32_t head = atomic_load(&ctrl->head);
        struct controlflow_batch *batch = (struct controlflow_batch *)&ctx->data_area[head];
        
        // 处理数据
        printf("Received %lu control flow entries:\n", batch->batch_size);
        for (uint64_t i = 0; i < batch->batch_size; ++i) {
            printf("Source ID: %llu, AddrToOffset: %llu\n", batch->data[i].source_id, batch->data[i].addrto_offset);
        }

        // 更新队列头指针
        atomic_store(&ctrl->head, (head + 1) % MAX_BATCH_SIZE);
        atomic_store(&ctrl->new_message, 0);
    }

    // 解锁
    atomic_store(&ctrl->lock, 0);
}

// 创建并初始化共享内存
struct shared_mem_ctx *init_shared_mem() {
    struct shared_mem_ctx *ctx = malloc(sizeof(struct shared_mem_ctx));
    if (!ctx) {
        printf("Failed to allocate shared memory context\n");
        return NULL;
    }

    // 初始化共享内存
    ctx->shm_base = malloc(sizeof(struct shm_control) + sizeof(struct hash_baseline) + MAX_BATCH_SIZE * sizeof(struct controlflow_info));
    if (!ctx->shm_base) {
        printf("Failed to allocate shared memory\n");
        free(ctx);
        return NULL;
    }

    // 设置上下文
    ctx->ctrl = (struct shm_control *)ctx->shm_base;
    ctx->baseline = (struct hash_baseline *)((char *)ctx->shm_base + sizeof(struct shm_control));
    ctx->data_area = (struct controlflow_info *)((char *)ctx->baseline + sizeof(struct hash_baseline));

    // 初始化控制块
    atomic_store(&ctx->ctrl->head, 0);
    atomic_store(&ctx->ctrl->tail, 0);
    ctx->ctrl->buffer_size = MAX_BATCH_SIZE;
    atomic_store(&ctx->ctrl->lock, 0);
    atomic_store(&ctx->ctrl->new_message, 0);

    // 初始化哈希基线
    ctx->baseline->generation = 1;
    TEE_GenerateRandom(ctx->baseline->initial_hash, TEE_HASH_SHA256_SIZE);
    atomic_store(&ctx->baseline->locked, 0);

    return ctx;
}

void cleanup_shared_mem(struct shared_mem_ctx *ctx) {
    free(ctx->shm_base);
    free(ctx);
}

int main() {
    struct shared_mem_ctx *ctx = init_shared_mem();
    if (!ctx) {
        return -1;
    }

    // 模拟度量代理写入控制流数据
    struct controlflow_batch batch = {
        .batch_size = 3,
        .data = {
            {1, 100},
            {2, 200},
            {3, 300}
        }
    };

    write_controlflow_data(ctx, &batch);

    // 模拟度量代理读取控制流数据
    read_controlflow_data(ctx);

    // 清理
    cleanup_shared_mem(ctx);
    return 0;
}
