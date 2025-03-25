// agent.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "agent.h"

#define TLS __thread

// 线程本地存储
TLS struct controlflow_batch thread_batch = {0};
TLS uint32_t batch_count = 0;
struct shared_mem_ctx *g_shared_ctx = NULL;

// 共享内存初始化
struct shared_mem_ctx *init_shared_mem(int is_creator) {
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) return NULL;

    if (is_creator && ftruncate(shm_fd, SHM_SIZE) == -1) {
        close(shm_fd);
        return NULL;
    }

    void *shm_base = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    if (shm_base == MAP_FAILED) return NULL;

    struct shared_mem_ctx *ctx = malloc(sizeof(struct shared_mem_ctx));
    ctx->is_creator = is_creator;
    ctx->ctrl = (struct shm_control *)shm_base;
    ctx->data_area = (struct controlflow_batch *)((char *)shm_base + sizeof(struct shm_control));

    if (is_creator) {
        atomic_init(&ctx->ctrl->head, 0);
        atomic_init(&ctx->ctrl->tail, 0);
        atomic_init(&ctx->ctrl->buffer_size, MAX_BATCH_SIZE);
        atomic_init(&ctx->ctrl->data_count, 0);
        atomic_flag_clear(&ctx->ctrl->lock);
    }

    return ctx;
}

// 原子写操作
void write_controlflow_data(struct shared_mem_ctx *ctx, struct controlflow_batch *batch) {
    // 自旋锁获取
    while (atomic_flag_test_and_set(&ctx->ctrl->lock)) 
        usleep(1);

    const uint32_t head = atomic_load(&ctx->ctrl->head);
    const uint32_t tail = atomic_load(&ctx->ctrl->tail);
    const uint32_t buf_size = atomic_load(&ctx->ctrl->buffer_size);

    if ((tail + 1) % buf_size != head) {
        memcpy(&ctx->data_area[tail], batch, sizeof(*batch));
        atomic_store(&ctx->ctrl->tail, (tail + 1) % buf_size);
        atomic_fetch_add(&ctx->ctrl->data_count, 1);
    }

    atomic_flag_clear(&ctx->ctrl->lock);
}

// 批量刷新
void flush_controlflow_batch() {
    if (batch_count > 0) {
        thread_batch.batch_size = batch_count;
        write_controlflow_data(g_shared_ctx, &thread_batch);
        batch_count = 0;
        memset(&thread_batch, 0, sizeof(thread_batch));
    }
}

// 添加控制流条目
void add_controlflow_entry(uint64_t source_bbid, uint64_t src_module_base, uint64_t target_offset) {
    if (!g_shared_ctx) {
        g_shared_ctx = init_shared_mem(0);
        if (!g_shared_ctx) return;
        atexit(flush_controlflow_batch);
    }

    thread_batch.data[batch_count] = (struct controlflow_info){
        .source_id = source_bbid,
        .addrto_offset = target_offset
    };
    if (++batch_count >= MAX_BATCH_SIZE) flush_controlflow_batch();
}

// 原子读操作
void read_controlflow_data(struct shared_mem_ctx *ctx) {
    // 等待数据可用
    while (atomic_load(&ctx->ctrl->data_count) == 0) 
        usleep(1000);

    // 自旋锁获取
    while (atomic_flag_test_and_set(&ctx->ctrl->lock)) 
        usleep(1);

    const uint32_t head = atomic_load(&ctx->ctrl->head);
    const uint32_t tail = atomic_load(&ctx->ctrl->tail);
    
    if (head != tail) {
        struct controlflow_batch *batch = &ctx->data_area[head];
        printf("[AGENT] Received %lu entries\n", batch->batch_size);

        // 新增：遍历并打印每个条目的详细信息
        for (uint64_t i = 0; i < batch->batch_size; ++i) {
            printf("Source ID: 0x%lx, Addrto Offset: 0x%lx\n",
                   batch->data[i].source_id,
                   batch->data[i].addrto_offset);
        }

        atomic_store(&ctx->ctrl->head, (head + 1) % atomic_load(&ctx->ctrl->buffer_size));
        atomic_fetch_sub(&ctx->ctrl->data_count, 1);
    } else {
        fprintf(stderr, "[DEBUG] No data to read\n");  // 可选：保留空分支的日志
    }

    atomic_flag_clear(&ctx->ctrl->lock);
}

// 清理共享内存
void cleanup_shared_mem(struct shared_mem_ctx *ctx) {
    if (ctx) {
        if (ctx->is_creator) {
            shm_unlink(SHM_NAME);
        }
        munmap(ctx->ctrl, SHM_SIZE);
        free(ctx);
    }
}

#ifdef AGENT_MAIN
int main() {
    struct shared_mem_ctx *ctx = init_shared_mem(1);
    if (!ctx) return -1;

    printf("[AGENT] Control Flow Monitor Started\n");
    while (1) {
        read_controlflow_data(ctx);
        usleep(10000);
    }

    cleanup_shared_mem(ctx);
    return 0;
}
#endif