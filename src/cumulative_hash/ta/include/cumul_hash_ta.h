#ifndef __CUMUL_HASH_TA_H__
#define __CUMUL_HASH_TA_H__


#define TA_CUMUL_HASH_UUID \
	{ 0x9bbd6f48, 0x9d95, 0x4a51, \
		{ 0xa6, 0xce, 0x90, 0xa9, 0x1a, 0x3c, 0x87, 0x75 } }

#define TEE_HASH_SHA256_SIZE 32
#define MAX_BATCH_SIZE 1024  // 例如，最多支持 1024 个 controlflow_info 结构体


struct controlflow_info {
    uint64_t source_id;            				// 源ID
    uint64_t addrto_offset;        				// 目的地址偏移量
    uint8_t hash[TEE_HASH_SHA256_SIZE];         // 递增哈希值
};

struct controlflow_batch {
    uint64_t batch_size;          // 当前批次的大小
    struct controlflow_info data[];  // 存储一批 controlflow_info的数组
};

#define TA_CUMUL_HASH_CMD_ACCUMULATE 0

#endif 