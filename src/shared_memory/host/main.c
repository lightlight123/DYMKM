#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <tee_client_api.h>
#include "shared_mem_ta.h"

// TA 操作句柄
struct test_ctx {
    TEEC_Context ctx;
    TEEC_Session sess;
};

// 全局上下文
static struct test_ctx ctx;

void prepare_tee_session(struct test_ctx *ctx) {
    TEEC_UUID uuid = TA_SHARED_MEM_UUID;
    uint32_t origin;
    TEEC_Result res;

    res = TEEC_InitializeContext(NULL, &ctx->ctx);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_InitializeContext failed with code 0x%x", res);

    res = TEEC_OpenSession(&ctx->ctx, &ctx->sess, &uuid,
                           TEEC_LOGIN_PUBLIC, NULL, NULL, &origin);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_OpenSession failed with code 0x%x origin 0x%x", res, origin);
}

void terminate_tee_session(struct test_ctx *ctx) {
    TEEC_CloseSession(&ctx->sess);
    TEEC_FinalizeContext(&ctx->ctx);
}

int enqueue_controlflow_batch(struct controlflow_batch *batch) {
    TEEC_Result res;
    uint32_t err_origin;
    TEEC_Operation op = {0};
    size_t batch_size = sizeof(struct controlflow_batch) + batch->batch_size * sizeof(struct controlflow_info);

    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].tmpref.buffer = batch;
    op.params[0].tmpref.size = batch_size;

    res = TEEC_InvokeCommand(&ctx.sess, TA_CMD_ENQUEUE, &op, &err_origin);
    if (res != TEEC_SUCCESS) {
        printf("Failed to invoke TA enqueue command with code 0x%x, origin 0x%x\n", res, err_origin);
        return -1;
    }
    return 0;
}

int main() {
    prepare_tee_session(&ctx);

    // 准备要发送的控制流信息
    struct controlflow_info infos[] = {
        { .source_id = 1, .addrto_offset = 0x1000 },
        { .source_id = 2, .addrto_offset = 0x2000 }
    };
    uint64_t batch_size = sizeof(infos) / sizeof(infos[0]);

    // 初始化哈希值，防止 TA 侧访问未初始化数据
    for (uint64_t i = 0; i < batch_size; i++) {
        memset(infos[i].hash, 0, TEE_HASH_SHA256_SIZE);
    }

    // 分配 batch 结构体
    size_t total_size = sizeof(struct controlflow_batch) + batch_size * sizeof(struct controlflow_info);
    struct controlflow_batch *batch = (struct controlflow_batch *)malloc(total_size);
    if (!batch) {
        printf("Memory allocation failed\n");
        terminate_tee_session(&ctx);
        return -1;
    }
    memset(batch, 0, total_size);

    batch->batch_size = batch_size;
    memcpy(batch->data, infos, batch_size * sizeof(struct controlflow_info));

    // 打印 batch 信息
    printf("Initialized batch with %lu entries\n", batch_size);
    for (uint64_t i = 0; i < batch_size; i++) {
        printf("Entry %lu: source_id=%lu, addrto_offset=0x%lx\n", i, batch->data[i].source_id, batch->data[i].addrto_offset);
    }

    // 调用 TA 进行数据传输
    if (enqueue_controlflow_batch(batch) != 0) {
        free(batch);
        terminate_tee_session(&ctx);
        return -1;
    }

    printf("Successfully enqueued control flow batch.\n");

    // 清理资源
    free(batch);
    terminate_tee_session(&ctx);
    return 0;
}
