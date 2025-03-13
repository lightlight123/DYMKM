#include <tee_internal_api.h>
#include <tee_internal_api_extensions.h>
#include <string.h>
#include "cumul_hash_ta.h"

// 函数原型声明
TEE_Result accumulate_controlflow_hash(struct controlflow_batch *batch);

// 累积哈希函数
TEE_Result accumulate_controlflow_hash(struct controlflow_batch *batch) {
    TEE_Result res = TEE_SUCCESS;
    TEE_OperationHandle operation = TEE_HANDLE_NULL;
    
    // 用于存储前一个哈希值，初始值为全 0
    uint8_t previous_hash[TEE_HASH_SHA256_SIZE] = {0};
    
    // 用于存储当前哈希输入数据
    uint8_t current_data[TEE_HASH_SHA256_SIZE + sizeof(uint64_t) * 2];

    // 参数有效性检查
    if (!batch || batch->batch_size == 0 || batch->batch_size > MAX_BATCH_SIZE) {
        EMSG("Invalid batch: %p size:%lu", batch, batch ? batch->batch_size : 0);
        return TEE_ERROR_BAD_PARAMETERS;
    }

    // 遍历每个控制流信息
    for (uint64_t i = 0; i < batch->batch_size; i++) {
        struct controlflow_info *info = &batch->data[i];
        if (!info) {
            EMSG("Null info at index:%lu", i);
            res = TEE_ERROR_BAD_PARAMETERS;
            break;
        }

        // 分配 SHA-256 哈希操作
        res = TEE_AllocateOperation(&operation, TEE_ALG_SHA256, TEE_MODE_DIGEST, 0);
        if (res != TEE_SUCCESS) {
            EMSG("TEE_AllocateOperation failed at index:%lu, res=0x%x", i, res);
            break;
        }

        // 构造当前哈希输入

        //将 current_data 初始化为全 0。
        TEE_MemFill(current_data, 0, sizeof(current_data));

        //将前一次哈希值 previous_hash复制到current_data 的开头。
        memcpy(current_data, previous_hash, TEE_HASH_SHA256_SIZE);

        //将info->source_id和info->addrto_offset复制到current_data的后续部分。
        memcpy(current_data + TEE_HASH_SHA256_SIZE, &info->source_id, sizeof(info->source_id));
        memcpy(current_data + TEE_HASH_SHA256_SIZE + sizeof(info->source_id), 
               &info->addrto_offset, sizeof(info->addrto_offset));

        DMSG("Hashing index %lu: source_id=0x%lx, addrto_offset=0x%lx", 
             i, info->source_id, info->addrto_offset);

        // 执行哈希计算

        //使用TEE_DigestUpdate将current_data添加到哈希操作中。
        TEE_DigestUpdate(operation, current_data, sizeof(current_data));

        uint32_t hash_len = TEE_HASH_SHA256_SIZE;

        //使用TEE_DigestDoFinal完成哈希计算，并将结果存储到previous_hash中。
        res = TEE_DigestDoFinal(operation, NULL, 0, previous_hash, &hash_len);
        if (res != TEE_SUCCESS || hash_len != TEE_HASH_SHA256_SIZE) {
            EMSG("Hash failed at index:%lu, res=0x%x len:%u", i, res, hash_len);
            break;
        }

        // 存储哈希结果
        //将当前哈希值previous_hash复制到info->hash中。
        memcpy(info->hash, previous_hash, TEE_HASH_SHA256_SIZE);

        // 释放操作句柄
        TEE_FreeOperation(operation);
        operation = TEE_HANDLE_NULL;
    }

    // 确保释放最后的操作句柄
    if (operation != TEE_HANDLE_NULL) {
        TEE_FreeOperation(operation);
    }

    return res;
}

TEE_Result TA_InvokeCommandEntryPoint(void __unused *session,
                                      uint32_t command,
                                      uint32_t param_types,
                                      TEE_Param params[4]) {
    // 验证参数类型
    const uint32_t exp_types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INOUT,
                                               TEE_PARAM_TYPE_NONE,
                                               TEE_PARAM_TYPE_NONE,
                                               TEE_PARAM_TYPE_NONE);
    if (param_types != exp_types) {
        EMSG("Invalid param types: 0x%x", param_types);
        return TEE_ERROR_BAD_PARAMETERS;
    }

    // 获取输入数据
    struct controlflow_batch *batch = (struct controlflow_batch *)params[0].memref.buffer;
    if (!batch) {
        EMSG("Batch pointer is NULL!");
        return TEE_ERROR_BAD_PARAMETERS;
    }

    // 计算最小缓冲区大小
    size_t min_size = sizeof(struct controlflow_batch) + 
                      batch->batch_size * sizeof(struct controlflow_info);

    // 验证缓冲区大小
    if (params[0].memref.size < min_size) {
        EMSG("Invalid buffer: size=%zu, required=%zu", 
             params[0].memref.size, min_size);
        return TEE_ERROR_BAD_PARAMETERS;
    }

    DMSG("Processing batch: size=%zubytes, num entries=%lu", 
        params[0].memref.size, batch->batch_size);
   

    // 调用哈希计算函数
    return accumulate_controlflow_hash(batch);
}


TEE_Result TA_CreateEntryPoint(void) {
    /* Nothing to do */
    return TEE_SUCCESS;
}

void TA_DestroyEntryPoint(void) {
    /* Nothing to do */
}

TEE_Result TA_OpenSessionEntryPoint(uint32_t __unused param_types,
                                    TEE_Param __unused params[4],
                                    void __unused **session) {
    /* Nothing to do */
    return TEE_SUCCESS;
}

void TA_CloseSessionEntryPoint(void __unused *session) {
    /* Nothing to do */
}