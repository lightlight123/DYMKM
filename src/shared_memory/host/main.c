#include <err.h>
#include <stdio.h>
#include <string.h>
#include <tee_client_api.h>
#include "shared_mem_ta.h"

typedef struct {
    size_t capacity;  // 数据区总容量
    size_t head;      // 写入位置
    size_t tail;      // 读取位置
    size_t count;     // 当前数据量
    uint8_t buffer[]; // 柔性数组指向数据区
} secure_queue_t;

#define QUEUE_HEAD_SIZE sizeof(secure_queue_t)
#define DATA_BUF_SIZE   4096
#define SHARED_BUF_SIZE (QUEUE_HEAD_SIZE + DATA_BUF_SIZE)

struct tee_ctx {
    TEEC_Context ctx;
    TEEC_Session sess;
    TEEC_SharedMemory shm;
};

void init_tee_session(struct tee_ctx *ctx) {
    TEEC_UUID uuid = TA_SHARED_MEM_UUID;
    TEEC_Result res;
    res = TEEC_InitializeContext(NULL, &ctx->ctx);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_InitContext failed 0x%x", res);

    res = TEEC_OpenSession(&ctx->ctx, &ctx->sess, &uuid,
                           TEEC_LOGIN_PUBLIC, NULL, NULL, NULL);
    if (res != TEEC_SUCCESS)
        errx(1, "TEEC_OpenSession failed 0x%x", res);

    ctx->shm.size = SHARED_BUF_SIZE;
    ctx->shm.flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
    res = TEEC_AllocateSharedMemory(&ctx->ctx, &ctx->shm);
    if (res != TEEC_SUCCESS || ctx->shm.buffer == NULL)
        errx(1, "TEEC_AllocSharedMem failed 0x%x", res);

    memset(ctx->shm.buffer, 0, SHARED_BUF_SIZE);
}

void enqueue_data(struct tee_ctx *ctx, const void *data, size_t len) {
    secure_queue_t *queue = (secure_queue_t *)ctx->shm.buffer;
    
    // 检查环形缓冲区是否已满
    if (queue->count == queue->capacity) {
        errx(1, "Queue is full");
    }

    // 将数据写入环形缓冲区
    uint8_t *data_area = (uint8_t *)ctx->shm.buffer + QUEUE_HEAD_SIZE;
    size_t write_pos = (queue->head) % queue->capacity;
    memcpy(data_area + write_pos, data, len);

    // 更新head和count
    queue->head = (queue->head + len) % queue->capacity;
    queue->count += len; // 改为按实际写入字节数更新

    // 调试信息
    printf("Enqueued %zu bytes: %s\n", len, (char *)data);
    printf("Queue state - head: %zu, tail: %zu, count: %zu\n", queue->head, queue->tail, queue->count);

    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT, TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE);
    op.params[0].memref.parent = &ctx->shm;
    op.params[0].memref.offset = QUEUE_HEAD_SIZE;
    op.params[0].memref.size = len;
    op.params[1].value.a = len;
    printf("len before TEEC_InvokeCommand: %zu\n", len);
    TEEC_Result res = TEEC_InvokeCommand(&ctx->sess, TA_CMD_ENQUEUE, &op, NULL);
    if (res != TEEC_SUCCESS)
        errx(1, "Enqueue failed 0x%x", res);
}

size_t dequeue_data(struct tee_ctx *ctx, void *buf, size_t buf_len) {
    secure_queue_t *queue = (secure_queue_t *)ctx->shm.buffer;

    // 调试输出
    printf("Queue state - head: %zu, tail: %zu, count: %zu, capacity: %zu\n", 
           queue->head, queue->tail, queue->count, queue->capacity);

    // 检查环形缓冲区是否为空
    if (queue->count == 0) {
        return 0;  // 队列为空，无数据可读取
    }

    // 从环形缓冲区读取数据
    uint8_t *data_area = (uint8_t *)ctx->shm.buffer + QUEUE_HEAD_SIZE;
    size_t read_pos = queue->tail;
    size_t data_len = 0;

    // 如果buf_len大于队列中的数据量，则调整buf_len
    if (buf_len > queue->count) {
        buf_len = queue->count;  // 读取最大可用数据
    }

    // 读取数据
    memcpy(buf, data_area + read_pos, buf_len);
    data_len = buf_len;
    printf("Before dequeue: queue->count: %zu, buf_len: %zu\n", queue->count, buf_len);

    // 更新tail和count
    queue->tail = (queue->tail + data_len) % queue->capacity;  // 更新tail，增加实际读取的字节数
    queue->count -= data_len;  // 更新count，减少读取的字节数

    // 调试信息
    printf("Dequeued %zu bytes: %s\n", data_len, (char *)buf);
    printf("Queue state - head: %zu, tail: %zu, count: %zu\n", queue->head, queue->tail, queue->count);

    TEEC_Operation op = {0};
    op.paramTypes = TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_OUTPUT, TEEC_VALUE_OUTPUT, TEEC_NONE, TEEC_NONE);
    op.params[0].memref.parent = &ctx->shm;
    op.params[0].memref.offset = QUEUE_HEAD_SIZE;
    op.params[0].memref.size = buf_len;  // 确保传递的是buf_len

    printf("buf_len before TEEC_InvokeCommand: %zu\n", buf_len);
    TEEC_Result res = TEEC_InvokeCommand(&ctx->sess, TA_CMD_DEQUEUE, &op, NULL);
    if (res != TEEC_SUCCESS)
        errx(1, "Dequeue failed 0x%x", res);

    return data_len;
}

int main() {
    struct tee_ctx ctx = {0};
    char test_data[] = "Hello Secure World!";
    char recv_buf[128] = {0};

    init_tee_session(&ctx);
    
    // 初始化共享内存中的队列
    secure_queue_t *queue = (secure_queue_t *)ctx.shm.buffer;
    queue->capacity = DATA_BUF_SIZE;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    
    enqueue_data(&ctx, test_data, sizeof(test_data));
    size_t recv_len = dequeue_data(&ctx, recv_buf, sizeof(recv_buf));
    
    printf("Received %zu bytes: %s\n", recv_len, recv_buf);

    TEEC_ReleaseSharedMemory(&ctx.shm);
    TEEC_CloseSession(&ctx.sess);
    TEEC_FinalizeContext(&ctx.ctx);
    return 0;
}
