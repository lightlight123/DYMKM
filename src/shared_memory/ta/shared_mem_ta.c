#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <string.h>
#include "shared_mem_ta.h"

#define ROUNDUP(x, align)    (((x) + (align) - 1) & ~((align) - 1))  // 自定义ROUNDUP宏，进行内存对齐

typedef uintptr_t vaddr_t;  // vaddr_t是指针类型，通常指代虚拟地址

TEE_Result TA_CreateEntryPoint(void) {
    return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void) {
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t param_types,
                                   TEE_Param params[4],
                                   void **sess_ctx) {
    (void)param_types; (void)params; // 消除未使用参数警告

    struct shared_mem_ctx *ctx;
    uint8_t *p;
    const size_t total_size = sizeof(struct shm_control) + 
                            sizeof(struct hash_baseline) +
                            MAX_BATCH_SIZE * sizeof(struct controlflow_info);

    DMSG("=== OpenSession ===");
    DMSG("Allocating context (%zu bytes)", sizeof(*ctx));
    // 分配上下文内存
    ctx = TEE_Malloc(sizeof(*ctx), TEE_MALLOC_FILL_ZERO);
    if (!ctx) {
        EMSG("Context alloc failed");
        return TEE_ERROR_OUT_OF_MEMORY;
    }

    DMSG("Allocating shared memory (%zu bytes)", total_size);
    // 分配共享内存，使用对齐的方式分配内存
    ctx->shm_base = TEE_Malloc(total_size, 0); 
    if (!ctx->shm_base) {
        EMSG("Shared mem alloc failed");
        TEE_Free(ctx);
        return TEE_ERROR_OUT_OF_MEMORY;
    }

    // 内存布局调试
    p = (uint8_t *)ctx->shm_base;
    ctx->ctrl = (struct shm_control *)ROUNDUP((vaddr_t)p, 8);
    DMSG("Control block @ %p (size:%zu)", 
            ctx->ctrl, sizeof(struct shm_control));
    
    p = (uint8_t*)ctx->ctrl + sizeof(struct shm_control);
    ctx->baseline = (struct hash_baseline *)ROUNDUP((vaddr_t)p, 8);
    DMSG("Baseline @ %p (size:%zu)", 
            ctx->baseline, sizeof(struct hash_baseline));
    
    p = (uint8_t*)ctx->baseline + sizeof(struct hash_baseline);
    ctx->data_area = (struct controlflow_info *)ROUNDUP((vaddr_t)p, 8);
    DMSG("Data area @ %p (capacity:%d)", 
            ctx->data_area, MAX_BATCH_SIZE);

    // 初始化原子变量
    atomic_store(&ctx->ctrl->head, 0);
    atomic_store(&ctx->ctrl->tail, 0);
    ctx->ctrl->buffer_size = MAX_BATCH_SIZE;
    atomic_store(&ctx->ctrl->lock, 0);

    // 初始化哈希基线
    TEE_GenerateRandom(ctx->baseline->initial_hash, TEE_HASH_SHA256_SIZE);
    ctx->baseline->generation = 1;
    atomic_store(&ctx->baseline->locked, 0);

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

// 安全生成哈希链
static TEE_Result generate_hash_chain(struct controlflow_info *entries, 
                                     uint32_t count, 
                                     const uint8_t *initial_hash) {
    TEE_OperationHandle op = TEE_HANDLE_NULL;
    uint8_t prev_hash[TEE_HASH_SHA256_SIZE];
    TEE_Result res = TEE_SUCCESS;
    
    memcpy(prev_hash, initial_hash, sizeof(prev_hash));
    
    for (uint32_t i = 0; i < count; i++) {
        // 打印条目基本信息
        DMSG("Processing entry %u: source_id=%" PRIu64 " offset=0x%" PRIx64, 
        i, entries[i].source_id, entries[i].addrto_offset);
        uint8_t input[TEE_HASH_SHA256_SIZE + sizeof(uint64_t)*2];
        
        // 构造输入数据
        memcpy(input, prev_hash, TEE_HASH_SHA256_SIZE);
        memcpy(input + TEE_HASH_SHA256_SIZE, &entries[i].source_id, sizeof(uint64_t));
        memcpy(input + TEE_HASH_SHA256_SIZE + sizeof(uint64_t), &entries[i].addrto_offset, sizeof(uint64_t));
        
        // 计算哈希
        res = TEE_AllocateOperation(&op, TEE_ALG_SHA256, TEE_MODE_DIGEST, 0);
        if (res != TEE_SUCCESS) goto exit;
        
        // 忽略 TEE_DigestUpdate 返回值
        TEE_DigestUpdate(op, input, sizeof(input));
        
        uint32_t hash_len = TEE_HASH_SHA256_SIZE;
        res = TEE_DigestDoFinal(op, NULL, 0, entries[i].hash, &hash_len);
        if (res != TEE_SUCCESS) goto exit;
        
        memcpy(prev_hash, entries[i].hash, TEE_HASH_SHA256_SIZE);
        TEE_FreeOperation(op);
        op = TEE_HANDLE_NULL;
    }
    
exit:
    if (op != TEE_HANDLE_NULL) TEE_FreeOperation(op);
    return res;
}

static TEE_Result enqueue_batch(struct shared_mem_ctx *ctx, struct controlflow_batch *batch) {
    DMSG("Enqueuing batch (size:%u)", batch->batch_size);
    uint32_t head, tail, free_space;
    const uint32_t buffer_size = ctx->ctrl->buffer_size;
    TEE_Result res = TEE_SUCCESS;
    
    // 原子加载队列状态
    head = atomic_load_explicit(&ctx->ctrl->head, memory_order_acquire);
    tail = atomic_load_explicit(&ctx->ctrl->tail, memory_order_acquire);
    DMSG("Queue status: head=%u, tail=%u, free=%u", 
        head, tail, (ctx->ctrl->buffer_size + tail - head - 1) % ctx->ctrl->buffer_size);
    free_space = (buffer_size + tail - head - 1) % buffer_size;
    
    if (free_space < batch->batch_size)
        return TEE_ERROR_SHORT_BUFFER;
    
    // 获取基线锁
    while (atomic_exchange_explicit(&ctx->baseline->locked, 1, memory_order_acq_rel) != 0)
        TEE_Wait(10);
    
    // 生成哈希链
    uint8_t *initial_hash = ctx->baseline->initial_hash;
    if (head != 0) { // 非首条数据，使用前一区块末哈希
        uint32_t last_pos = (head - 1) % buffer_size;
        memcpy(initial_hash, ctx->data_area[last_pos].hash, TEE_HASH_SHA256_SIZE);
    }
    
    res = generate_hash_chain(batch->data, batch->batch_size, initial_hash);
    if (res != TEE_SUCCESS) {
        atomic_store_explicit(&ctx->baseline->locked, 0, memory_order_release);
        return res;
    }
    
    // 获取队列锁
    while (atomic_exchange_explicit(&ctx->ctrl->lock, 1, memory_order_acq_rel) != 0)
        TEE_Wait(10);
    
    // 写入数据
    for (uint32_t i = 0; i < batch->batch_size; i++) {
        uint32_t pos = (head + i) % buffer_size;
        memcpy(&ctx->data_area[pos], &batch->data[i], sizeof(struct controlflow_info));
    }
    
    // 更新队列头指针
    atomic_store_explicit(&ctx->ctrl->head, (head + batch->batch_size) % buffer_size, memory_order_release);
    atomic_store_explicit(&ctx->ctrl->lock, 0, memory_order_release);
    atomic_store_explicit(&ctx->baseline->locked, 0, memory_order_release);
    
    return TEE_SUCCESS;
}

static TEE_Result verify_chain_hash(struct shared_mem_ctx *ctx,
                                  uint32_t batch_size) {
    DMSG("Verifying chain (size:%u)", batch_size);
    TEE_OperationHandle op = TEE_HANDLE_NULL;
    uint8_t prev_hash[TEE_HASH_SHA256_SIZE];
    uint8_t calc_hash[TEE_HASH_SHA256_SIZE];
    TEE_Result res = TEE_SUCCESS;
    
    // 获取基线初始哈希
    memcpy(prev_hash, ctx->baseline->initial_hash, TEE_HASH_SHA256_SIZE);
    
    for (uint32_t i = 0; i < batch_size; i++) {
        struct controlflow_info *info = &ctx->data_area[i];
        DMSG("Verifying entry %u: source_id=%" PRIu64 " hash=", 
            i, info->source_id);
        
        // 打印存储的哈希值
        for (int j = 0; j < TEE_HASH_SHA256_SIZE; j++) {
            DMSG_RAW("%02x", info->hash[j]);
        }
        DMSG_RAW("\n");
        uint8_t input[TEE_HASH_SHA256_SIZE + sizeof(uint64_t)*2];
        
        // 构造输入数据
        memcpy(input, prev_hash, TEE_HASH_SHA256_SIZE);
        memcpy(input + TEE_HASH_SHA256_SIZE, &info->source_id, sizeof(uint64_t));
        memcpy(input + TEE_HASH_SHA256_SIZE + sizeof(uint64_t), &info->addrto_offset, sizeof(uint64_t));
        
        // 计算哈希
        res = TEE_AllocateOperation(&op, TEE_ALG_SHA256, TEE_MODE_DIGEST, 0);
        if (res != TEE_SUCCESS) goto exit;
        
        // 忽略 TEE_DigestUpdate 返回值
        TEE_DigestUpdate(op, input, sizeof(input));
        
        uint32_t hash_len = sizeof(calc_hash);
        res = TEE_DigestDoFinal(op, NULL, 0, calc_hash, &hash_len);
        if (res != TEE_SUCCESS) goto exit;
        
        // 对比哈希值
        if (memcmp(calc_hash, info->hash, TEE_HASH_SHA256_SIZE) != 0) {
            res = TEE_ERROR_SECURITY;
            goto exit;
        }
        
        memcpy(prev_hash, calc_hash, TEE_HASH_SHA256_SIZE);
        TEE_FreeOperation(op);
        op = TEE_HANDLE_NULL;
    }
    
exit:
    if (op != TEE_HANDLE_NULL) TEE_FreeOperation(op);
    return res;
}


static TEE_Result process_batch(struct shared_mem_ctx *ctx) {
    uint32_t head, tail, batch_size;
    TEE_Result res = TEE_SUCCESS;
    
    head = atomic_load_explicit(&ctx->ctrl->head, memory_order_acquire);
    tail = atomic_load_explicit(&ctx->ctrl->tail, memory_order_acquire);
    batch_size = (head >= tail) ? (head - tail) : (ctx->ctrl->buffer_size - tail + head);
    
    if (batch_size == 0) return TEE_SUCCESS;
    
    // 获取队列锁
    while (atomic_exchange_explicit(&ctx->ctrl->lock, 1, memory_order_acq_rel) != 0)
        TEE_Wait(10);
    
    res = verify_chain_hash(ctx, batch_size);
    if (res == TEE_SUCCESS) {
        atomic_store_explicit(&ctx->ctrl->tail, (tail + batch_size) % ctx->ctrl->buffer_size, 
                            memory_order_release);
    }
    
    atomic_store_explicit(&ctx->ctrl->lock, 0, memory_order_release);
    return res;
}

TEE_Result TA_InvokeCommandEntryPoint(void *sess_ctx,
                                     uint32_t cmd_id,
                                     uint32_t param_types,
                                     TEE_Param params[4]) {
    struct shared_mem_ctx *ctx = (struct shared_mem_ctx *)sess_ctx;
    
    switch (cmd_id) {
    case TA_CMD_ENQUEUE:
        if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_MEMREF_INPUT)
            return TEE_ERROR_BAD_PARAMETERS;
        return enqueue_batch(ctx, params[0].memref.buffer);
        
    case TA_CMD_PROCESS:
        if (TEE_PARAM_TYPE_GET(param_types, 0) != TEE_PARAM_TYPE_VALUE_INOUT)
            return TEE_ERROR_BAD_PARAMETERS;
        params[0].value.a = process_batch(ctx);
        return TEE_SUCCESS;
        
    default:
        return TEE_ERROR_NOT_IMPLEMENTED;
    }
}