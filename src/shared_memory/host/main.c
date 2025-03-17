#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <tee_client_api.h>
#include "shared_mem_ta.h"

#define DEBUG_ENABLE 1

#define DPRINTF(fmt, ...) \
    do { if (DEBUG_ENABLE) printf("[HOST] " fmt, ##__VA_ARGS__); } while (0)

struct test_ctx {
    TEEC_Context ctx;
    TEEC_Session sess;
};

// 初始化TEE会话（需补充实现）
static void prepare_tee_session(struct test_ctx *ctx) {
    TEEC_UUID uuid = TA_SHARED_MEM_UUID;
    TEEC_Result res;
    uint32_t origin;
    
    res = TEEC_InitializeContext(NULL, &ctx->ctx);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_InitializeContext failed: 0x%x", res);
    
    res = TEEC_OpenSession(&ctx->ctx, &ctx->sess, &uuid,
                          TEEC_LOGIN_PUBLIC, NULL, NULL, &origin);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_OpenSession failed: 0x%x (origin 0x%x)", res, origin);
}

// 生成原始测试数据（哈希字段由TA填充）
static struct controlflow_batch* generate_test_batch(size_t count) {
    size_t total_size = sizeof(struct controlflow_batch) + 
                       count * sizeof(struct controlflow_info);
    struct controlflow_batch* batch = malloc(total_size);
    
    if (!batch) {
        DPRINTF("Memory allocation failed\n");
        return NULL;
    }
    
    memset(batch, 0, total_size);
    batch->batch_size = count;

    // 仅填充原始数据，不计算哈希
    for (size_t i = 0; i < count; i++) {
        batch->data[i].source_id = i + 1;
        batch->data[i].addrto_offset = 0x1000 * (i + 1);
        DPRINTF("Generated entry %zu: source_id=%lu, offset=0x%lx\n", 
               i, batch->data[i].source_id, batch->data[i].addrto_offset);
    }
    return batch;
}

int main() {
    struct test_ctx ctx = {0};
    TEEC_Result res;
    uint32_t err_origin;

    prepare_tee_session(&ctx);
    DPRINTF("TEE session initialized\n");

    // 生成测试数据（3条记录）
    const size_t test_count = 3;
    struct controlflow_batch* batch = generate_test_batch(test_count);
    if (!batch) {
        TEEC_CloseSession(&ctx.sess);
        TEEC_FinalizeContext(&ctx.ctx);
        return EXIT_FAILURE;
    }

    /******************** 入队操作 ********************/
    TEEC_Operation enqueue_op = {0};
    enqueue_op.paramTypes = TEEC_PARAM_TYPES(
        TEEC_MEMREF_TEMP_INPUT,
        TEEC_NONE,
        TEEC_NONE,
        TEEC_NONE
    );
    enqueue_op.params[0].tmpref.buffer = batch;
    enqueue_op.params[0].tmpref.size = sizeof(struct controlflow_batch) + 
                                      test_count * sizeof(struct controlflow_info);

    DPRINTF("Invoking TA_CMD_ENQUEUE...\n");
    if ((res = TEEC_InvokeCommand(&ctx.sess, TA_CMD_ENQUEUE, &enqueue_op, &err_origin)) != TEEC_SUCCESS) {
        fprintf(stderr, "Enqueue failed: 0x%x (origin 0x%x)\n", res, err_origin);
        goto cleanup;
    }
    DPRINTF("Enqueued %zu entries successfully\n", test_count);

    /******************** 处理操作 ********************/
    TEEC_Operation process_op = {0};
    process_op.paramTypes = TEEC_PARAM_TYPES(
        TEEC_VALUE_INOUT,
        TEEC_NONE,
        TEEC_NONE,
        TEEC_NONE
    );

    DPRINTF("Invoking TA_CMD_PROCESS...\n");
    if ((res = TEEC_InvokeCommand(&ctx.sess, TA_CMD_PROCESS, &process_op, &err_origin)) != TEEC_SUCCESS) {
        fprintf(stderr, "Process failed: 0x%x (TA error: 0x%x)\n", 
               res, process_op.params[0].value.a);
    } else {
        DPRINTF("Batch processed successfully\n");
        DPRINTF("TA returned status: 0x%x\n", process_op.params[0].value.a);
    }

cleanup:
    free(batch);
    TEEC_CloseSession(&ctx.sess);
    TEEC_FinalizeContext(&ctx.ctx);
    DPRINTF("TEE resources released\n");
    return (res == TEEC_SUCCESS) ? EXIT_SUCCESS : EXIT_FAILURE;
}