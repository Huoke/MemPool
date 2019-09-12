
/*
 */

#include "config.h"
#if HAVE_ASSERT_H
#include <assert.h>
#endif

#include "MemPool.h"
#include "MemPoolChunked.h"
#include "MemPoolMalloc.h"

#define FLUSH_LIMIT 1000	/* Flush memPool counters to memMeters after flush limit calls */

#if HAVE_STRING_H
#include <string.h>
#endif

/*
 * XXX This is a boundary violation between lib and src.. would be good
 * if it could be solved otherwise, but left for now.
 */
extern time_t squid_curtime;

/* local data */
static MemPoolMeter TheMeter;
static MemPoolIterator Iterator;

static int Pool_id_counter = 0;

MemPools &MemPools::GetInstance()
{
    /* 必须使用这种风格, 如果在静态初始化期间，再有人调用可能造成两次初始的情况 */
    if (!Instance)
        Instance = new MemPools;
    return *Instance;
}

MemPools * MemPools::Instance = NULL;

MemPoolIterator *memPoolIterate(void)
{
    Iterator.pool = MemPools::GetInstance().pools; // 内存分配器
    return &Iterator;
}

void memPoolIterateDone(MemPoolIterator ** iter)
{
    assert(iter != NULL);
    Iterator.pool = NULL;
    *iter = NULL;
}

MemImplementingAllocator *memPoolIterateNext(MemPoolIterator * iter)
{
    MemImplementingAllocator *pool; // 内存分配器
    assert(iter != NULL);

    pool = iter->pool;// 把当前的内存分配器赋值给 临时指针变量
    if (!pool)
        return NULL;

    iter->pool = pool->next; // 当前的内存分配器的下一个分配器 赋值给 迭代器中下一个分配器
    return pool; // 返回临时内存分配器
    // 此时迭代器已经指向下一个内存分配器，这里返回当前的内存分配器
}

void MemPools::setIdleLimit(ssize_t new_idle_limit)
{
    mem_idle_limit = new_idle_limit;
}

ssize_t MemPools::idleLimit() const
{
    return mem_idle_limit;
}

/* 修改所有内存池的 defaultIsChunked的默认值，包括在main函数前MemPools::GetInstance().setDefaultPoolChunking()设置的值
  Change the default calue of defaultIsChunked to override all pools - including those used before main() starts where
 * MemPools::GetInstance().setDefaultPoolChunking() can be called.
 */
MemPools::MemPools() : pools(NULL), mem_idle_limit(2 * MB),
        poolCount (0), defaultIsChunked (USE_CHUNKEDMEMPOOLS && !RUNNING_ON_VALGRIND)
{
    char *cfg = getenv("MEMPOOLS");
    if (cfg)
        defaultIsChunked = atoi(cfg);
}

MemImplementingAllocator* MemPools::create(const char *label, size_t obj_size)
{
    ++poolCount; // 池计数器增加
    if (defaultIsChunked) // 默认按照块分配
        return new MemPoolChunked (label, obj_size);
    else                  // 按照大小分配
        return new MemPoolMalloc (label, obj_size);
}

void MemPools::setDefaultPoolChunking(bool const &aBool)
{
    defaultIsChunked = aBool;
}

char const *MemAllocator::objectType() const
{
    return label;
}

int MemAllocator::inUseCount()
{
    return getInUseCount();
}

void MemImplementingAllocator::flushMeters()
{
    size_t calls;

    calls = free_calls;
    if (calls) {
        meter.gb_freed.count += calls;
        free_calls = 0;
    }
    
    calls = alloc_calls;
    if (calls) {
        meter.gb_allocated.count += calls;
        alloc_calls = 0;
    }
    calls = saved_calls;
    if (calls) {
        meter.gb_saved.count += calls;
        saved_calls = 0;
    }
}

void MemImplementingAllocator::flushMetersFull()
{
    flushMeters();
    getMeter().gb_allocated.bytes = getMeter().gb_allocated.count * obj_size;
    getMeter().gb_saved.bytes = getMeter().gb_saved.count * obj_size;
    getMeter().gb_freed.bytes = getMeter().gb_freed.count * obj_size;
}

void MemPoolMeter::flush()
{
    alloc.level = 0;
    inuse.level = 0;
    idle.level = 0;
    gb_allocated.count = 0;
    gb_allocated.bytes = 0;
    gb_oallocated.count = 0;
    gb_oallocated.bytes = 0;
    gb_saved.count = 0;
    gb_saved.bytes = 0;
    gb_freed.count = 0;
    gb_freed.bytes = 0;
}

MemPoolMeter::MemPoolMeter()
{
    flush();
}

/*
 * Updates all pool counters, and recreates TheMeter totals from all pools
 * 更新所有池计数器，并从所有池中重新创建TheMeter总计
 */
void MemPools::flushMeters()
{
    MemImplementingAllocator *pool;
    MemPoolIterator *iter;

    TheMeter.flush();

    iter = memPoolIterate();
    while ((pool = memPoolIterateNext(iter))) 
    {
        pool->flushMetersFull();
        memMeterAdd(TheMeter.alloc, pool->getMeter().alloc.level * pool->obj_size);
        memMeterAdd(TheMeter.inuse, pool->getMeter().inuse.level * pool->obj_size);
        memMeterAdd(TheMeter.idle, pool->getMeter().idle.level * pool->obj_size);
        TheMeter.gb_allocated.count += pool->getMeter().gb_allocated.count;
        TheMeter.gb_saved.count += pool->getMeter().gb_saved.count;
        TheMeter.gb_freed.count += pool->getMeter().gb_freed.count;

        TheMeter.gb_allocated.bytes += pool->getMeter().gb_allocated.bytes;
        TheMeter.gb_saved.bytes += pool->getMeter().gb_saved.bytes;
        TheMeter.gb_freed.bytes += pool->getMeter().gb_freed.bytes;
    }
    memPoolIterateDone(&iter);
}

void *MemImplementingAllocator::alloc()
{
    if (++alloc_calls == FLUSH_LIMIT)
        flushMeters();

    return allocate();
}

void MemImplementingAllocator::free(void *obj)
{
    assert(obj != NULL);
    (void) VALGRIND_CHECK_MEM_IS_ADDRESSABLE(obj, obj_size);
    deallocate(obj, MemPools::GetInstance().mem_idle_limit == 0);
    ++free_calls;
}

/*
 * Returns all cached frees to their home chunks
 * If chunks unreferenced age is over, destroys Idle chunk
 * Flushes meters for a pool
 * If pool is not specified, iterates through all pools.
 * When used for all pools, if new_idle_limit is above -1, new
 * idle memory limit is set before Cleanup. This allows to shrink
 * memPool memory usage to specified minimum.
 */
void MemPools::clean(time_t maxage)
{
    flushMeters();
    if (mem_idle_limit < 0) // no limit to enforce
        return;

    int shift = 1;
    if (TheMeter.idle.level > mem_idle_limit)
        maxage = shift = 0;

    MemImplementingAllocator *pool;
    MemPoolIterator *iter;
    iter = memPoolIterate();
    while ((pool = memPoolIterateNext(iter)))
        if (pool->idleTrigger(shift))
            pool->clean(maxage);
    memPoolIterateDone(&iter);
}

/* Persistent Pool stats. for GlobalStats accumulation */
static MemPoolStats pp_stats;

/*
 * Totals statistics is returned
 */
int memPoolGetGlobalStats(MemPoolGlobalStats * stats)
{
    int pools_inuse = 0;
    MemAllocator *pool;
    MemPoolIterator *iter;

    memset(stats, 0, sizeof(MemPoolGlobalStats));
    memset(&pp_stats, 0, sizeof(MemPoolStats));

    MemPools::GetInstance().flushMeters(); /* recreate TheMeter */

    /* gather all stats for Totals */
    iter = memPoolIterate();
    while ((pool = memPoolIterateNext(iter))) {
        if (pool->getStats(&pp_stats, 1) > 0)
            pools_inuse++;
    }
    memPoolIterateDone(&iter);

    stats->TheMeter = &TheMeter;

    stats->tot_pools_alloc = MemPools::GetInstance().poolCount;
    stats->tot_pools_inuse = pools_inuse;
    stats->tot_pools_mempid = Pool_id_counter;

    stats->tot_chunks_alloc = pp_stats.chunks_alloc;
    stats->tot_chunks_inuse = pp_stats.chunks_inuse;
    stats->tot_chunks_partial = pp_stats.chunks_partial;
    stats->tot_chunks_free = pp_stats.chunks_free;
    stats->tot_items_alloc = pp_stats.items_alloc;
    stats->tot_items_inuse = pp_stats.items_inuse;
    stats->tot_items_idle = pp_stats.items_idle;

    stats->tot_overhead += pp_stats.overhead + MemPools::GetInstance().poolCount * sizeof(MemAllocator *);
    stats->mem_idle_limit = MemPools::GetInstance().mem_idle_limit;

    return pools_inuse;
}

MemAllocator::MemAllocator(char const *aLabel) : doZeroOnPush(true), label(aLabel)
{
}

size_t MemAllocator::RoundedSize(size_t s)
{
    return ((s + sizeof(void*) - 1) / sizeof(void*)) * sizeof(void*);
}

int memPoolInUseCount(MemAllocator * pool)
{
    return pool->inUseCount();
}

int memPoolsTotalAllocated(void)
{
    MemPoolGlobalStats stats;
    memPoolGetGlobalStats(&stats);
    return stats.TheMeter->alloc.level;
}

void *MemAllocatorProxy::alloc()
{
    return getAllocator()->alloc();
}

void MemAllocatorProxy::free(void *address)
{
    getAllocator()->free(address);
    /* TODO: check for empty, and if so, if the default type has altered,
     * switch
     */
}

MemAllocator *MemAllocatorProxy::getAllocator() const
{
    if (!theAllocator)
        theAllocator = MemPools::GetInstance().create(objectType(), size);
    return theAllocator;
}

int MemAllocatorProxy::inUseCount() const
{
    if (!theAllocator)
        return 0;
    else
        return memPoolInUseCount(theAllocator);
}

size_t MemAllocatorProxy::objectSize() const
{
    return size;
}

char const *MemAllocatorProxy::objectType() const
{
    return label;
}

MemPoolMeter const &MemAllocatorProxy::getMeter() const
{
    return getAllocator()->getMeter();
}

int MemAllocatorProxy::getStats(MemPoolStats * stats)
{
    return getAllocator()->getStats(stats);
}

MemImplementingAllocator::MemImplementingAllocator(char const *aLabel, size_t aSize) : MemAllocator(aLabel),
        next(NULL),
        alloc_calls(0), // 分配调用次数
        free_calls(0),  // 释放调用次数
        saved_calls(0), 
        obj_size(RoundedSize(aSize))
{
    memPID = ++Pool_id_counter;  // 内存池id计数器

    MemImplementingAllocator *last_pool;

    assert(aLabel != NULL && aSize);
    /* Append as Last */
    for (last_pool = MemPools::GetInstance().pools; last_pool && last_pool->next;)
        last_pool = last_pool->next;
    if (last_pool)
        last_pool->next = this;
    else
        MemPools::GetInstance().pools = this;
}

MemImplementingAllocator::~MemImplementingAllocator()
{
    MemImplementingAllocator *find_pool, *prev_pool;

    assert(MemPools::GetInstance().pools != NULL && "Called MemImplementingAllocator::~MemImplementingAllocator, but no pool exists!");

    /* Pool clean, remove it from List and free */
    for (find_pool = MemPools::GetInstance().pools, prev_pool = NULL; (find_pool && this != find_pool); find_pool = find_pool->next)
        prev_pool = find_pool;
    assert(find_pool != NULL && "pool to destroy not found");

    if (prev_pool)
        prev_pool->next = next;
    else
        MemPools::GetInstance().pools = next;
    --MemPools::GetInstance().poolCount;
}

void
MemAllocator::zeroOnPush(bool doIt)
{
    doZeroOnPush = doIt;
}

MemPoolMeter const& MemImplementingAllocator::getMeter() const
{
    return meter;
}

MemPoolMeter & MemImplementingAllocator::getMeter()
{
    return meter;
}

size_t MemImplementingAllocator::objectSize() const
{
    return obj_size;
}
