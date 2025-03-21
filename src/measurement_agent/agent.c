#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include "agent.h"



// 线程本地存储优化（兼容C/C++）
#if defined(__cplusplus)
  #define TLS thread_local
#else
  #define TLS __thread
#endif

// TLS缓存
TLS struct controlflow_batch thread_batch = {0};
TLS uint32_t batch_count = 0;
struct shared_mem_ctx *g_shared_ctx = NULL;

// 共享内存初始化（is_creator=1表示创建者）
struct shared_mem_ctx *init_shared_mem(int is_creator) {
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        return NULL;
    }

    if (is_creator) {
        if (ftruncate(shm_fd, SHM_SIZE) == -1) {
            perror("ftruncate failed");
            close(shm_fd);
            return NULL;
        }
    }

    void *shm_base = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd); // 及时关闭 fd
    if (shm_base == MAP_FAILED) {
        perror("mmap failed");
        return NULL;
    }

    struct shared_mem_ctx *ctx = malloc(sizeof(struct shared_mem_ctx));
    ctx->is_creator = is_creator;
    ctx->ctrl = (struct shm_control *)shm_base;
    ctx->data_area = (struct controlflow_batch *)((char *)shm_base + sizeof(struct shm_control));

    if (is_creator) {
        sem_init(&ctx->ctrl->lock, 1, 1);
        sem_init(&ctx->ctrl->data_ready, 1, 0);
        ctx->ctrl->head = 0;
        ctx->ctrl->tail = 0;
        ctx->ctrl->buffer_size = MAX_BATCH_SIZE;
    }

    return ctx;
}


// 写入控制流数据（线程安全）
void write_controlflow_data(struct shared_mem_ctx *ctx, struct controlflow_batch *batch) {
    sem_wait(&ctx->ctrl->lock);

    uint32_t next_tail = (ctx->ctrl->tail + 1) % ctx->ctrl->buffer_size;
    if (next_tail != ctx->ctrl->head) {
        memcpy(&ctx->data_area[ctx->ctrl->tail], batch, sizeof(*batch));
        __sync_synchronize(); // 确保写入顺序正确
        ctx->ctrl->tail = next_tail;
        sem_post(&ctx->ctrl->data_ready);
    } else {
        fprintf(stderr, "[DEBUG] Shared memory buffer is full, skipping write\n");
    }

    sem_post(&ctx->ctrl->lock);
}


void flush_controlflow_batch() {
    if (batch_count > 0) {
        thread_batch.batch_size = batch_count;
        write_controlflow_data(g_shared_ctx, &thread_batch);
        batch_count = 0;
        memset(&thread_batch, 0, sizeof(thread_batch));
    }
}

void add_controlflow_entry(uint64_t source_bbid, uint64_t src_module_base, uint64_t target_offset) {
    if (!g_shared_ctx) {
        g_shared_ctx = init_shared_mem(0);
        if (!g_shared_ctx) {
            fprintf(stderr, "[ERROR] Shared memory init failed\n");
            return;
        }
        atexit(flush_controlflow_batch); // 确保退出时flush
    }

    thread_batch.data[batch_count].source_id = source_bbid;
    thread_batch.data[batch_count].addrto_offset = target_offset;
    batch_count++;

    if (batch_count >= MAX_BATCH_SIZE) {
        flush_controlflow_batch();
    }
}

// 读取控制流数据（Agent主循环）
void read_controlflow_data(struct shared_mem_ctx *ctx) {
    sem_wait(&ctx->ctrl->data_ready);  // 等待数据通知
    sem_wait(&ctx->ctrl->lock);

    if (ctx->ctrl->head != ctx->ctrl->tail) {
        struct controlflow_batch *batch = &ctx->data_area[ctx->ctrl->head];
        printf("[AGENT] Received %lu entries:\n", batch->batch_size);
        
        // 解析实际地址（需在目标程序中实现ASLR基地址获取）
        for (uint64_t i = 0; i < batch->batch_size; ++i) {
            printf("Source ID: 0x%lx, Addrto Offset: 0x%lx\n",
                   batch->data[i].source_id, 
                   batch->data[i].addrto_offset);
        }
        
        ctx->ctrl->head = (ctx->ctrl->head + 1) % ctx->ctrl->buffer_size;
    } else {
        fprintf(stderr, "[DEBUG] No data to read\n");
    }

    sem_post(&ctx->ctrl->lock);
}

// 清理共享内存
void cleanup_shared_mem(struct shared_mem_ctx *ctx) {
    if (ctx) {
        if (ctx->is_creator) {
            sem_destroy(&ctx->ctrl->lock);
            sem_destroy(&ctx->ctrl->data_ready);
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
        usleep(10000);  // 降低CPU占用
    }

    cleanup_shared_mem(ctx);
    return 0;
}
#endif