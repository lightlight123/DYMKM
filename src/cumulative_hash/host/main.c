#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <tee_client_api.h>
#include "cumul_hash_ta.h"

// TA操作句柄
struct test_ctx {
    TEEC_Context ctx;
    TEEC_Session sess;
};

// 全局上下文
static struct test_ctx ctx;

void prepare_tee_session(struct test_ctx *ctx) {
    TEEC_UUID uuid = TA_CUMUL_HASH_UUID;
    uint32_t origin;
    TEEC_Result res;

    // 初始化上下文
    res = TEEC_InitializeContext(NULL, &ctx->ctx);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_InitializeContext failed with code 0x%x", res);

    // 打开会话
    res = TEEC_OpenSession(&ctx->ctx, &ctx->sess, &uuid,
                           TEEC_LOGIN_PUBLIC, NULL, NULL, &origin);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_OpenSession failed with code 0x%x origin 0x%x", res, origin);
}

void terminate_tee_session(struct test_ctx *ctx) {
    TEEC_CloseSession(&ctx->sess);
    TEEC_FinalizeContext(&ctx->ctx);
}

// 调用TA执行累积哈希
int test_accumulate_hash(struct controlflow_batch *batch) {
    TEEC_Result res;
    uint32_t err_origin;
    TEEC_Operation op = {0};

    // 计算实际传输大小（包含 controlflow_info 数据）
    size_t batch_size = sizeof(struct controlflow_batch) + batch->batch_size * sizeof(struct controlflow_info);

    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INOUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    op.params[0].tmpref.buffer = batch;
    op.params[0].tmpref.size = batch_size;

    res = TEEC_InvokeCommand(&ctx.sess, TA_CUMUL_HASH_CMD_ACCUMULATE, &op, &err_origin);
    if (res != TEEC_SUCCESS) {
        printf("Failed to invoke TA command with code 0x%x, origin 0x%x\n", res, err_origin);
        return -1;
    }

    // 打印每个 controlflow_info 的哈希值
    for (uint64_t i = 0; i < batch->batch_size; i++) {
        printf("Controlflow %lu: Hash = ", (unsigned long)i);
        for (int j = 0; j < TEE_HASH_SHA256_SIZE; j++) {
            printf("%02x", batch->data[i].hash[j]);
        }
        printf("\n");
    }

    return 0;
}

int main() {
    prepare_tee_session(&ctx);

    // 定义批量数据
    struct controlflow_info infos[] = {
        { .source_id = 1, .addrto_offset = 0x1000 },
        { .source_id = 2, .addrto_offset = 0x2000 }
    };
    uint64_t batch_size = sizeof(infos) / sizeof(infos[0]);

    // 计算总大小，并分配连续内存
    size_t total_size = sizeof(struct controlflow_batch) + batch_size * sizeof(struct controlflow_info);
    struct controlflow_batch *batch = (struct controlflow_batch *)malloc(total_size);
    if (!batch) {
        printf("Memory allocation failed\n");
        terminate_tee_session(&ctx);
        return -1;
    }
    memset(batch, 0, total_size);

    // 填充 batch 结构
    batch->batch_size = batch_size;
    memcpy(batch->data, infos, batch_size * sizeof(struct controlflow_info));

    printf("Initialized batch with %lu entries\n", batch_size);
    for (uint64_t i = 0; i < batch_size; i++) {
        printf("Entry %lu: source_id=%lu, addrto_offset=0x%lx\n",
               i, batch->data[i].source_id, batch->data[i].addrto_offset);
    }

    // 调用 TA 进行哈希操作
    if (test_accumulate_hash(batch) != 0) {
        free(batch);
        terminate_tee_session(&ctx);
        return -1;
    }

    // 释放资源
    free(batch);
    terminate_tee_session(&ctx);
    return 0;
}
