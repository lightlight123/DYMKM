#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include "agent.h"

#define SHM_NAME "/cf_shm"    // 共享内存名称
#define SEM_LOCK_NAME "/cflock" // 互斥信号量名称
#define SEM_DATA_NAME "/cfdata" // 数据通知信号量名称

// 线程本地存储优化（每个线程独立batch）
__thread struct controlflow_batch thread_batch;
__thread uint32_t batch_count = 0;


// 共享内存初始化（is_creator=1表示创建者）
struct shared_mem_ctx *init_shared_mem(int is_creator) {
    static struct shared_mem_ctx ctx;
    size_t shm_size = sizeof(struct shm_control) + 
                     MAX_BATCH_SIZE * sizeof(struct controlflow_batch);

    // 创建/打开共享内存 
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        return NULL;
    }

    // 设置共享内存大小（仅创建者需要）
    if (is_creator && ftruncate(shm_fd, shm_size) == -1) {
        perror("ftruncate failed");
        close(shm_fd);
        return NULL;
    }

    // 内存映射 
    ctx.shm_base = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, 
                       MAP_SHARED, shm_fd, 0);
    if (ctx.shm_base == MAP_FAILED) {
        perror("mmap failed");
        close(shm_fd);
        return NULL;
    }
    close(shm_fd);  // 映射后即可关闭文件描述符

    // 初始化结构体指针
    ctx.ctrl = (struct shm_control*)ctx.shm_base;
    ctx.data_area = (struct controlflow_batch*)((char*)ctx.shm_base + 
                       sizeof(struct shm_control));

    // 初始化信号量 
    if (is_creator) {
        // 初始化队列控制参数
        ctx.ctrl->head = 0;
        ctx.ctrl->tail = 0;
        ctx.ctrl->buffer_size = MAX_BATCH_SIZE;
        
        // 创建新的信号量 
        sem_unlink(SEM_LOCK_NAME);
        sem_unlink(SEM_DATA_NAME);
        ctx.sem_lock = sem_open(SEM_LOCK_NAME, O_CREAT, 0666, 1);
        ctx.sem_data = sem_open(SEM_DATA_NAME, O_CREAT, 0666, 0);
    } else {
        // 打开已存在的信号量
        ctx.sem_lock = sem_open(SEM_LOCK_NAME, 0);
        ctx.sem_data = sem_open(SEM_DATA_NAME, 0);
    }

    if (ctx.sem_lock == SEM_FAILED || ctx.sem_data == SEM_FAILED) {
        perror("sem_open failed");
        munmap(ctx.shm_base, shm_size);
        return NULL;
    }

    return &ctx;
}

void write_controlflow_data(struct shared_mem_ctx *ctx, 
                           struct controlflow_batch *batch) {
    sem_wait(ctx->sem_lock);  // 获取互斥锁 

    // 环形队列写入 
    uint32_t next_tail = (ctx->ctrl->tail + 1) % ctx->ctrl->buffer_size;
    if (next_tail != ctx->ctrl->head) {
        memcpy(&ctx->data_area[ctx->ctrl->tail], batch, sizeof(*batch));
        ctx->ctrl->tail = next_tail;
        sem_post(ctx->sem_data);  // 通知有新数据 
    }

    sem_post(ctx->sem_lock);  // 释放互斥锁
}

void read_controlflow_data(struct shared_mem_ctx *ctx) {
    sem_wait(ctx->sem_data);   // 等待数据通知 
    sem_wait(ctx->sem_lock);   // 获取互斥锁

    if (ctx->ctrl->head != ctx->ctrl->tail) {
        struct controlflow_batch *batch = &ctx->data_area[ctx->ctrl->head];
        
        printf("[AGENT] Received %lu entries:\n", batch->batch_size);
        for (uint64_t i = 0; i < batch->batch_size; ++i) {
            printf("Source ID: %lu, Offset: %lu\n",
                   batch->data[i].source_id, 
                   batch->data[i].addrto_offset);
        }
        
        ctx->ctrl->head = (ctx->ctrl->head + 1) % ctx->ctrl->buffer_size;
    }

    sem_post(ctx->sem_lock);  // 释放互斥锁
}

void cleanup_shared_mem(struct shared_mem_ctx *ctx) {
    if (ctx) {
        sem_close(ctx->sem_lock);
        sem_close(ctx->sem_data);
        munmap(ctx->shm_base, sizeof(struct shm_control) + 
              MAX_BATCH_SIZE * sizeof(struct controlflow_batch));
        if (ctx->sem_lock) sem_unlink(SEM_LOCK_NAME);
        if (ctx->sem_data) sem_unlink(SEM_DATA_NAME);
    }
}

int main() {
    // 以创建者身份初始化（参数1表示创建共享内存）
    struct shared_mem_ctx *ctx = init_shared_mem(1);
    if (!ctx) return -1;

    printf("[AGENT] Control Flow Monitor Started\n");
    
    while (1) {
        read_controlflow_data(ctx);
        // 降低CPU占用（可根据需要调整）
        usleep(5000);  
    }

    cleanup_shared_mem(ctx);
    return 0;
}