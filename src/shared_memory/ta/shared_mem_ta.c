#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include "shared_mem_ta.h"
#include <string.h>

typedef struct {
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    uint8_t buffer[];
} secure_queue_t;

// 先声明函数
static TEE_Result init_queue(uint32_t paramTypes, TEE_Param params[4]);
static TEE_Result handle_enqueue(uint32_t paramTypes, TEE_Param params[4]);
static TEE_Result handle_dequeue(uint32_t paramTypes, TEE_Param params[4]);

static secure_queue_t *queue = NULL;
static uint8_t *data_buffer = NULL;

TEE_Result TA_CreateEntryPoint(void) {
    DMSG("TA Create Entry Point");
    return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void) {
    if (queue) {
        TEE_Free(queue);
        queue = NULL;
    }
    data_buffer = NULL;
    DMSG("TA Destroyed");
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t paramTypes, TEE_Param params[4], void **sessionContext) {
    (void)paramTypes;
    (void)params;

    queue = TEE_Malloc(sizeof(secure_queue_t) + 4096, TEE_MALLOC_FILL_ZERO);
    if (!queue) {
        EMSG("Failed to allocate queue memory");
        return TEE_ERROR_OUT_OF_MEMORY;
    }

    queue->capacity = 4096;
    queue->head = queue->tail = queue->count = 0;
    data_buffer = queue->buffer;

    *sessionContext = queue;
    DMSG("TA Opened. Queue allocated at %p", queue);
    return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void *sessionContext) {
    if (sessionContext) {
        TEE_Free(sessionContext);
    }
    queue = NULL;
    data_buffer = NULL;
    DMSG("TA Closed");
}

TEE_Result TA_InvokeCommandEntryPoint(void *sessionContext, uint32_t cmdID, uint32_t paramTypes, TEE_Param params[4]) {
    if (!sessionContext) {
        EMSG("Invalid session context");
        return TEE_ERROR_BAD_STATE;
    }

    queue = (secure_queue_t *)sessionContext;
    data_buffer = queue->buffer;

    switch (cmdID) {
        case TA_CMD_INIT_QUEUE:
            return init_queue(paramTypes, params);
        case TA_CMD_ENQUEUE:
            return handle_enqueue(paramTypes, params);
        case TA_CMD_DEQUEUE:
            return handle_dequeue(paramTypes, params);
        default:
            EMSG("Unknown command ID: 0x%x", cmdID);
            return TEE_ERROR_NOT_SUPPORTED;
    }
}

static TEE_Result init_queue(uint32_t paramTypes, TEE_Param params[4]) {
    if (paramTypes != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
                                      TEE_PARAM_TYPE_VALUE_INPUT,
                                      TEE_PARAM_TYPE_NONE,
                                      TEE_PARAM_TYPE_NONE)) {
        EMSG("Bad parameters in init_queue");
        return TEE_ERROR_BAD_PARAMETERS;
    }

    queue = (secure_queue_t *)params[0].memref.buffer;
    queue->capacity = params[1].value.a;

    if (queue->capacity > params[0].memref.size - sizeof(secure_queue_t)) {
        EMSG("Queue capacity exceeds shared memory size");
        return TEE_ERROR_SHORT_BUFFER;
    }

    queue->head = queue->tail = queue->count = 0;
    data_buffer = queue->buffer;
    DMSG("Queue initialized. Capacity: %zu", queue->capacity);
    return TEE_SUCCESS;
}

static TEE_Result handle_enqueue(uint32_t paramTypes, TEE_Param params[4]) {
    if (paramTypes != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
                                      TEE_PARAM_TYPE_VALUE_INPUT,
                                      TEE_PARAM_TYPE_NONE,
                                      TEE_PARAM_TYPE_NONE)) {
        EMSG("Bad parameters in handle_enqueue");
        return TEE_ERROR_BAD_PARAMETERS;
    }

    if (!queue || !data_buffer) {
        EMSG("Queue or data buffer is NULL");
        return TEE_ERROR_BAD_STATE;
    }

    size_t data_len = params[1].value.a;
    if (data_len == 0 || data_len > params[0].memref.size) {
        EMSG("Invalid data length: %zu", data_len);
        return TEE_ERROR_BAD_PARAMETERS;
    }

    if (queue->count + data_len > queue->capacity) {
        EMSG("Not enough space in queue for data. Queue count: %zu, Data len: %zu", queue->count, data_len);
        return TEE_ERROR_OUT_OF_MEMORY;
    }

    size_t write_pos = queue->head;
    size_t first_chunk = queue->capacity - write_pos;

    if (data_len <= first_chunk) {
        memcpy(&data_buffer[write_pos], params[0].memref.buffer, data_len);
    } else {
        memcpy(&data_buffer[write_pos], params[0].memref.buffer, first_chunk);
        memcpy(data_buffer, (uint8_t *)params[0].memref.buffer + first_chunk, data_len - first_chunk);
    }

    queue->head = (queue->head + data_len) % queue->capacity;
    queue->count += data_len;
    DMSG("Enqueued data. New head: %zu, count: %zu", queue->head, queue->count);
    return TEE_SUCCESS;
}

static TEE_Result handle_dequeue(uint32_t paramTypes, TEE_Param params[4]) {
    if (paramTypes != TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
                                      TEE_PARAM_TYPE_VALUE_OUTPUT,
                                      TEE_PARAM_TYPE_NONE,
                                      TEE_PARAM_TYPE_NONE)) {
        return TEE_ERROR_BAD_PARAMETERS;
    }

    if (!queue || !data_buffer || queue->count == 0) {
        return TEE_ERROR_NO_DATA;
    }

    // 获取请求的大小
    size_t requested_size = params[0].memref.size;
    
    // 获取队列中当前可用的字节数
    size_t available_data = queue->count;

    // 打印调试信息，查看请求的大小和队列当前的可用数据量
    DMSG("Requested size: %zu, Available data: %zu", requested_size, available_data);

    // 如果请求的大小大于队列中的数据，则调整为队列中可用的数据量
    if (requested_size > available_data) {
        requested_size = available_data;
    }

    // 设置返回的实际大小
    params[1].value.a = requested_size;

    size_t read_pos = queue->tail;
    size_t first_chunk = queue->capacity - read_pos;

    // 如果请求的大小小于等于从尾部到队列末尾的数据量，直接复制
    if (requested_size <= first_chunk) {
        memcpy(params[0].memref.buffer, &data_buffer[read_pos], requested_size);
    } else {
        // 如果请求的大小大于从尾部到队列末尾的数据量，则分两次复制
        memcpy(params[0].memref.buffer, &data_buffer[read_pos], first_chunk);
        memcpy((uint8_t *)params[0].memref.buffer + first_chunk, data_buffer, requested_size - first_chunk);
    }

    // 更新队列的尾部和数据计数
    queue->tail = (queue->tail + requested_size) % queue->capacity;  // 确保tail环绕
    queue->count -= requested_size;

    // 打印调试信息，显示出队列状态
    DMSG("Dequeued %zu bytes. Queue state - head: %zu, tail: %zu, count: %zu",
         requested_size, queue->head, queue->tail, queue->count);

    return TEE_SUCCESS;
}
