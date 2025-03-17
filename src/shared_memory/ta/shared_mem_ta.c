#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <string.h>
#include "shared_mem_ta.h"

TEE_Result TA_CreateEntryPoint(void) {
    return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void) {
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types,
                                    TEE_Param params[4],
                                    void **sess_ctx) {
    struct shared_mem_ctx *ctx;
    //共享内存大小
    size_t shm_size = sizeof(struct shm_control) + MAX_BATCH_SIZE * sizeof(struct controlflow_info);
    
    ctx = TEE_Malloc(sizeof(struct shared_mem_ctx), TEE_MALLOC_FILL_ZERO);
    if (!ctx)
        return TEE_ERROR_OUT_OF_MEMORY;
    
    ctx->shm_base = TEE_Malloc(shm_size, TEE_MALLOC_FILL_ZERO);
    if (!ctx->shm_base) {
        TEE_Free(ctx);
        return TEE_ERROR_OUT_OF_MEMORY;
    }
    
    ctx->ctrl = (struct shm_control *)ctx->shm_base;
    ctx->data_area = (struct controlflow_info *)((char *)ctx->shm_base + sizeof(struct shm_control));
    
    atomic_init(&ctx->ctrl->head, 0);
    atomic_init(&ctx->ctrl->tail, 0);
    ctx->ctrl->buffer_size = MAX_BATCH_SIZE;
    atomic_init(&ctx->ctrl->lock, 0);
    
    *sess_ctx = ctx;
    return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *sess_ctx) {
    struct shared_mem_ctx *ctx = (struct shared_mem_ctx *)sess_ctx;
    if (ctx) {
        TEE_Free(ctx->shm_base);
        TEE_Free(ctx);
    }
}

static TEE_Result enqueue_batch(struct shared_mem_ctx *ctx, struct controlflow_batch *batch) {
    uint32_t head, tail, free_space;
    const uint32_t buffer_size = ctx->ctrl->buffer_size;

    //使用原子操作加载head和tail的值，确保同步访问。
    //memory_order_acquire 表示获取操作之前的所有读取操作不能被重排到获取操作之后。
    head = atomic_load_explicit(&ctx->ctrl->head, memory_order_acquire);
    tail = atomic_load_explicit(&ctx->ctrl->tail, memory_order_acquire);
    
    free_space = (buffer_size + tail - head - 1) & (buffer_size - 1);
    if (free_space < batch->batch_size)
        return TEE_ERROR_SHORT_BUFFER;

    //通过原子交换 (atomic_exchange_explicit)操作尝试获取lock锁。
    //如果当前锁已被其他线程持有，atomic_exchange_explicit会返回非零值，然后调用TEE_Wait(10)等待10毫秒后重试。
    //这确保只有一个线程可以写入缓冲区。
    while (atomic_exchange_explicit(&ctx->ctrl->lock, 1, memory_order_acq_rel) != 0)
        TEE_Wait(10);
    //写入控制流信息到缓冲区
    for (uint32_t i = 0; i < batch->batch_size; i++) {
        uint32_t pos = (head + i) % buffer_size;
        memcpy(&ctx->data_area[pos], &batch->data[i], sizeof(struct controlflow_info));
    }
    //更新head
    atomic_store_explicit(&ctx->ctrl->head, (head + batch->batch_size) % buffer_size, memory_order_release);
    //设置new_message为1，表示有新消息可以处理
    atomic_store_explicit(&ctx->ctrl->new_message, 1, memory_order_release);
    //释放锁
    atomic_store_explicit(&ctx->ctrl->lock, 0, memory_order_release);
    
    return TEE_SUCCESS;
}

TEE_Result TA_InvokeCommandEntryPoint(void *sess_ctx,
                                      uint32_t cmd_id,
                                      uint32_t param_types,
                                      TEE_Param params[4]) {
    struct shared_mem_ctx *ctx = (struct shared_mem_ctx *)sess_ctx;
    struct controlflow_batch *batch;

    if (cmd_id == TA_CMD_ENQUEUE) {
        if (param_types != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT, TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE))
            return TEE_ERROR_BAD_PARAMETERS;

        batch = (struct controlflow_batch *)params[0].memref.buffer;
        return enqueue_batch(ctx, batch);
    }
    return TEE_ERROR_NOT_IMPLEMENTED;
}
