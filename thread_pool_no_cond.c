

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#define LL_ADD(item, list) do {      \
    item->next = list;               \
    if (list) list->prev = item;     \
    list = item;                     \
} while (0)

#define LL_REMOVE(item, list) do {      		     \
    if (item->prev) item->prev->next = item->next;   \
    if (item->next) item->next->prev = item->prev;   \
    if (list == item) list = item->next;             \
    item->prev = item->next = NULL;                  \
} while (0)

// 任务队列
typedef struct NJOB {
    void (*job_func)(void *arg);
    void *arg;
    struct NJOB *next;
    struct NJOB *prev;
}job_t;

// 工作线程
typedef struct NWORKER {
    pthread_t        thread_id;
    struct NMANAGER *pool;
    struct NWORKER  *next;
    struct NWORKER  *prev;
}worker_t;

typedef struct NMANAGER {
    job_t           *jobs;       // 任务队列的头结点
    worker_t        *workers;    // 执行队列的头结点（线程集合）
    int       		 terminate;  // 销毁线程池
    pthread_mutex_t  mtx;        // 加锁防止出现多个线程抢同一个任务的情况
}thread_pool_t;


void *nThreadCallback(void *arg){
    worker_t * worker = (worker_t *)arg;
    while (1)
    {
		// 无条件变量版本：此处使用【忙等】，会消耗大量的CPU
		while (worker->pool->jobs == NULL) {
			if (worker->pool->terminate) { // 打上终止标记，就结束线程
				printf("terminate!!!\n");
				goto end;
			};
		}

	    pthread_mutex_lock(&worker->pool->mtx); 
		{
			if (worker->pool->jobs == NULL) { // double-check
				pthread_mutex_unlock(&worker->pool->mtx);
				continue;
			}
			
			// 任务队列中有任务，就取出任务，执行之
			job_t *job = worker->pool->jobs;
			if (job) {
				LL_REMOVE(job, worker->pool->jobs);
			}	
		}
        pthread_mutex_unlock(&worker->pool->mtx);

		job->job_func(job);
		
		free(job->arg);
		free(job);
		job = NULL;
    }
end:
    pthread_mutex_unlock(&worker->pool->mtx);
}

int nThreadPoolPushTask(thread_pool_t *pool, job_t *job){
    pthread_mutex_lock(&pool->mtx);
	{
		LL_ADD(job, pool->jobs);	
	}
    pthread_mutex_unlock(&pool->mtx);
}

int nThreadPoolCreate(thread_pool_t *pool, int worker_nums) {
    if (!pool) return -ENOMEM;
    memset(pool, 0, sizeof(thread_pool_t));

    if (worker_nums < 1) 
		worker_nums = 1;

    pthread_mutex_init(&(pool->mtx), NULL);

    int i = 0;
    for (i ; i < worker_nums; ++i) { 
        worker_t *worker = (worker_t *)calloc(1, sizeof(worker_t));
        if (!worker)
            return -ENOMEM;
		
        worker->pool = pool;

        int ret = pthread_create(&worker->thread_id, NULL, nThreadCallback, worker);
        if (ret){
            free(worker);
            return -ENOMEM;
        }

        LL_ADD(worker, pool->workers);    
    }

    return 0;
}

int nThreadPoolDestroy(thread_pool_t *pool) {
    // 防止多次调用释放资源
    if (pool->terminate)
        return 0;
    
    pool->terminate = 1;

    // 等待线程全部执行完成
    worker_t *head = pool->workers;
    while (head){
        pthread_join(head->thread_id, NULL);
        worker_t *temp = head;
        head = head->next; 
        LL_REMOVE(temp, pool->workers);
        if (temp){
            free(temp);
            temp = NULL;
        }
    }
            
    pthread_mutex_destroy(&pool->mtx);
    free(pool);
    pool = NULL;
	
    return 0;
}

#if 1
#define WORKER_NUMS  80
#define COUNTER_SIZE 1000

void jobCbk (void *arg) {
    job_t *job = (job_t *)arg;
    printf("counter --> %d\n", *(int *)job->arg);
}

int main() {
    thread_pool_t *pool = (thread_pool_t*)calloc(1, sizeof(thread_pool_t));

    nThreadPoolCreate(pool, WORKER_NUMS);
    
	// 添加job到任务队列
    int i = 0;
    for (i = 0; i < COUNTER_SIZE; i++) {
        job_t *job = (job_t *)calloc(1, sizeof(job_t));
        if (!job) {
            return -ENOMEM;
        }

        job->job_func = jobCbk;
        job->arg = (int *)calloc(1, sizeof(int));
        *((int *)job->arg ) = i;
		
        nThreadPoolPushTask(pool, job);
    }
	
	// 销毁线程池
    nThreadPoolDestroy(pool);
}

#endif