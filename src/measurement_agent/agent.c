#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>
#include "agent.h"


// 线程本地存储优化
__thread struct controlflow_batch thread_batch;
__thread uint32_t batch_count = 0;

struct shared_mem_ctx *g_shared_ctx = NULL;

void write_controlflow_data(struct shared_mem_ctx *ctx, struct controlflow_batch *batch) {
    struct shm_control *ctrl = ctx->ctrl;
    
    while (atomic_exchange_explicit(&ctrl->lock, 1, memory_order_acquire)) {}

    uint32_t next_tail = (atomic_load(&ctrl->tail) + 1) % ctrl->buffer_size;
    if (next_tail != atomic_load(&ctrl->head)) {
        memcpy(&ctx->data_area[atomic_load(&ctrl->tail)], batch, sizeof(*batch));
        atomic_store(&ctrl->tail, next_tail);
        atomic_store(&ctrl->new_message, 1);
    }

    atomic_store_explicit(&ctrl->lock, 0, memory_order_release);
}

void add_controlflow_entry(uint64_t source_id, uint64_t offset) {
    if (!g_shared_ctx) return;

    // 批量处理逻辑
    if (batch_count >= MAX_BATCH_SIZE) {
        thread_batch.batch_size = MAX_BATCH_SIZE;
        write_controlflow_data(g_shared_ctx, &thread_batch);
        batch_count = 0;
        memset(&thread_batch, 0, sizeof(thread_batch));
    }

    thread_batch.data[batch_count].source_id = source_id;
    thread_batch.data[batch_count].addrto_offset = offset;
    batch_count++;
}

struct shared_mem_ctx *init_shared_mem() {
    if (!g_shared_ctx) {
        g_shared_ctx = malloc(sizeof(struct shared_mem_ctx));
        size_t shm_size = sizeof(struct shm_control) + 
                        MAX_BATCH_SIZE * sizeof(struct controlflow_batch);
        g_shared_ctx->shm_base = malloc(shm_size);
        
        g_shared_ctx->ctrl = (struct shm_control*)g_shared_ctx->shm_base;
        g_shared_ctx->data_area = (struct controlflow_batch*)
            ((char*)g_shared_ctx->shm_base + sizeof(struct shm_control));
        
        atomic_init(&g_shared_ctx->ctrl->head, 0);
        atomic_init(&g_shared_ctx->ctrl->tail, 0);
        g_shared_ctx->ctrl->buffer_size = MAX_BATCH_SIZE;
        atomic_init(&g_shared_ctx->ctrl->lock, 0);
        atomic_init(&g_shared_ctx->ctrl->new_message, 0);
    }
    return g_shared_ctx;
}


void read_controlflow_data(struct shared_mem_ctx *ctx) {
    struct shm_control *ctrl = ctx->ctrl;
    
    while (atomic_exchange(&ctrl->lock, 1)) {}

    if (atomic_load(&ctrl->new_message)) {
        uint32_t head = atomic_load(&ctrl->head);
        struct controlflow_batch *batch = &ctx->data_area[head];
        
        printf("[AGENT] Received %lu control flow entries:\n", batch->batch_size);
        for (uint64_t i = 0; i < batch->batch_size; ++i) {
            printf("[AGENT] Source ID: %lu, AddrToOffset: %lu\n", 
                   batch->data[i].source_id, 
                   batch->data[i].addrto_offset);
        }

        atomic_store(&ctrl->head, (head + 1) % ctrl->buffer_size);
        atomic_store(&ctrl->new_message, 0);
    }

    atomic_store(&ctrl->lock, 0);
}


void cleanup_shared_mem(struct shared_mem_ctx *ctx) {
    free(ctx->shm_base);
    free(ctx);
}

int main() {
    struct shared_mem_ctx *ctx = init_shared_mem();
    if (!ctx) return -1;

    printf("[AGENT] Control Flow Agent Started. Waiting for data...\n");

    while (1) {
        read_controlflow_data(ctx);
        usleep(10000); // 10ms 间隔，避免高 CPU 占用
    }

    cleanup_shared_mem(ctx);
    return 0;
}
