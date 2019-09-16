#ifndef _MEM_POOL_H_
#define _MEM_POOL_H_
/*********************************************************************************************
 * 内存池分配器
 * 内存池是运行在 malloc（）上的池内存分配器。它的目的是减少内存碎片，并提供有关内存消耗的详细统计信息。
 * Squid中的所有内存分配最好使用 mempools或在它上面构建的类型（即cbdata）来完成。
 * 注意:
 * 通常，最好使用cbdata类型，因为这些类型在引用和类型检查中为您提供了额外的保护。 
 * 但是, 对于不需要直接使用cbdata功能的高使用率池，可以使用内存池。
 *********************************************************************************************/
#include "config.h"
#include "util.h"
#include "memMeter.h"
#include "splay.h"
#include <malloc.h>
#include <memory.h>

#if !M_MMAP_MAX
#if USE_DLMALLOC
#define M_MMAP_MAX -4
#endif
#endif

// in group MemPoolsAPI
#define MB ((size_t)1024*1024) // 1MB 单位
// 转换为MB
#define toMB(size) ( ((double) size) / MB )
// 转换为KB
#define toKB(size) ( (size + 1024 - 1) / 1024 )

/// \ingroup MemPoolsAPI
#define MEM_PAGE_SIZE 4096

// 内存块大小 8kb
#define MEM_CHUNK_SIZE 4096 * 4
// 最大内存块 256kb
#define MEM_CHUNK_MAX_SIZE  256 * 1024	/* 2MB  32*8*1024 */
// 
#define MEM_MIN_FREE  32
// 
#define MEM_MAX_FREE  65535	/* ushort is max number of items per chunk */

class MemImplementingAllocator;
class MemPoolStats;

// todo Kill this typedef for C++
typedef struct _MemPoolGlobalStats MemPoolGlobalStats;

// 内存池迭代器
class MemPoolIterator
{
public:
    MemImplementingAllocator *pool;
    MemPoolIterator * next;
};

/* 跟踪每个池的累计计数器 */
class mgb_t
{
public:
    mgb_t() : count(0), bytes(0) {}
    double count;
    double bytes; // 为啥用double 而不是 int 呢？
};

/* 跟踪每个池的使用情况 (alloc = inuse+idle) alloc = 使用的 + 空闲的 */
class MemPoolMeter
{
public:
    MemPoolMeter();
    void flush();
    MemMeter alloc;
    MemMeter inuse;
    MemMeter idle;


    /** history Allocations */
    mgb_t gb_allocated;
    mgb_t gb_oallocated;

    /** account Saved Allocations */
    mgb_t gb_saved;

    /** account Free calls */
    mgb_t gb_freed;
};

class MemImplementingAllocator;

// 单例模式
class MemPools
{
public:
    static MemPools &GetInstance();
    MemPools(); // 构造函数怎么也暴露出来了
    void init();
    void flushMeters();

    /*******************************************************
    label: 内存池名称，在统计中显示.
    obj_size: 内存池元素的大小.
    ******************************************************/
    MemImplementingAllocator* create(const char *label, size_t obj_size);

    /**
     * 以字节设置内存池中可用内存的上限。这不是严格的设置，而是一个提示 
     * 当超过这个上限，考虑立即释放所有空闲内存块。否则，检查长时间未使用的内存块 
     */
    void setIdleLimit(ssize_t new_idle_limit);
    ssize_t idleLimit() const;

    /**
     * 主清理程序，为了保持内存池空闲上限 
     * 这个函数需要定期的被调用， 
     * 最好以一个恒定周期调用，比如从squid的事件调用 
     * 查看所有内存池和块 清理内部状态，检测可发布的块 
     *
     *在对这个函数调用期间，对象被放置到内部缓存中，而不是返回到其主块，主要是为了加速。
     *在此期间，块的状态未知，不知道块是空闲的还是正在使用中。
     *此调用将所有对象返回到其块并恢复一致性。
     *
     * 频繁的调用clean, 因为它会按适当的顺序对块进行排序，减少内存碎片提高内存块的使用率
     * 根据内存使用情况，clean调用的频率在几十秒到几分钟之间
     *param maxage: 释放所有在maxage秒内没有被引用的完全空闲内存块.
     */
    void clean(time_t maxage);

    void setDefaultPoolChunking(bool const &);
    MemImplementingAllocator *pools;
    ssize_t mem_idle_limit;
    int poolCount;
    bool defaultIsChunked;
private:
    static MemPools *Instance;
};

/***************************************************************
 * 内部内存池API
 * 对于大小相同的对象，一个空间不断增长的池
 * 内存分配器
 **************************************************************/
class MemAllocator
{
public:
    MemAllocator (char const *aLabel);
    virtual ~MemAllocator() {}

    /*param stats 对象用来统计内存池数据.
      retval： 返回值是正在使用的对象数，即已分配的对象数.*/
    virtual int getStats(MemPoolStats * stats, int accumulate = 0) = 0;

    virtual MemPoolMeter const &getMeter() const = 0;

    /*从池中分配一个元素*/
    virtual void *alloc() = 0;

    /* 释放一个在池中已经分配的元素*/
    virtual void free(void *) = 0;

    virtual char const *objectType() const;
    virtual size_t objectSize() const = 0;
    virtual int getInUseCount() = 0;
    void zeroOnPush(bool doIt);
    int inUseCount();

    /**
     * 允许设置内存池的大小，对象是在内存块中分配的而不是单独分配 
     * 这样可以节省内存，减少碎片由于内存只能以块的形式释放
     * 因此需要在内存碎片和保护内存分块之间权衡 
     注意:  在内存池中，许多项目保存时间较长，原则上只增加块的大小
     */
    virtual void setChunkSize(size_t chunksize) {} // 设置块的大小

    /* param minSize：最小需要分配多大.
       retval n:  返回能被sizeof(void*)整除的最小大小 */
    static size_t RoundedSize(size_t minSize);

protected:
    bool doZeroOnPush;

private:
    const char *label;
};

/******************************************************************
 * 内部内存池API
 * 后期支持不区分分配器类型的内存池类型绑定
 * **************************************************************/
class MemAllocatorProxy
{
public:
    inline MemAllocatorProxy(char const *aLabel, size_t const &);

    /* 从内存池中分配一个元素 */
    void *alloc();

    /* 释放掉使用MemAllocatorProxy::alloc()分配的内存元素 */
    void free(void *);

    int inUseCount() const; // 已经使用的内存计数器
    size_t objectSize() const;

    MemPoolMeter const &getMeter() const;

    /*param stats 对象用来统计内存池数据.
      retval： 返回值是正在使用的对象数，即已分配的对象数.*/
    int getStats(MemPoolStats * stats);

    char const * objectType() const;
private:
    MemAllocator *getAllocator() const;
    const char *label;
    size_t size;
    mutable MemAllocator *theAllocator; // 内存分配器
};

// 一下是帮助宏定义
/*******************************************************
 * 组内部 MemPoolsAPI
 * 隐藏初始化 ，MEMPROXY_CLASS 宏用在类的声明中
 ******************************************************/
#define MEMPROXY_CLASS(CLASS) \
    inline void *operator new(size_t); \
    inline void operator delete(void *); \
    static inline MemAllocatorProxy &Pool()

/*******************************************************
 * 组内部 MemPoolsAPI
 * 隐藏初始化， 此宏用于类的.h或.cci中（视情况而定）。
 ******************************************************/
#define MEMPROXY_CLASS_INLINE(CLASS) \
MemAllocatorProxy& CLASS::Pool() \
{ \
    static MemAllocatorProxy thePool(#CLASS, sizeof (CLASS)); \
    return thePool; \
} \
\
void * \
CLASS::operator new (size_t byteCount) \
{ \
    /* derived classes with different sizes must implement their own new */ \
    assert (byteCount == sizeof (CLASS)); \
\
    return Pool().alloc(); \
}  \
\
void \
CLASS::operator delete (void *address) \
{ \
    Pool().free(address); \
}

/* 内存分配器实现 */
class MemImplementingAllocator : public MemAllocator
{
public:
    MemImplementingAllocator(char const *aLabel, size_t aSize);
    virtual ~MemImplementingAllocator();
    virtual MemPoolMeter const &getMeter() const;
    virtual MemPoolMeter &getMeter();
    virtual void flushMetersFull();
    virtual void flushMeters();

    /* 在内存池中分配一个块内存 */
    virtual void *alloc();

    /*通过 MemImplementingAllocator::alloc()分配一个空闲元素*/
    virtual void free(void *);

    virtual bool idleTrigger(int shift) const = 0;
    virtual void clean(time_t maxage) = 0;
    virtual size_t objectSize() const;
    virtual int getInUseCount() = 0;
protected:
    virtual void *allocate() = 0;
    virtual void deallocate(void *, bool aggressive) = 0;
    MemPoolMeter meter;
    int memPID;
public:
    MemImplementingAllocator *next;
public:
    size_t alloc_calls;
    size_t free_calls;
    size_t saved_calls;
    size_t obj_size;
};

class MemPoolStats
{
public:
    MemAllocator *pool;
    const char *label;
    MemPoolMeter *meter;
    int obj_size;
    int chunk_capacity;
    int chunk_size;

    int chunks_alloc;
    int chunks_inuse;
    int chunks_partial;
    int chunks_free;

    int items_alloc;
    int items_inuse;
    int items_idle;

    int overhead;
};

struct _MemPoolGlobalStats {
    MemPoolMeter *TheMeter;

    int tot_pools_alloc;
    int tot_pools_inuse;
    int tot_pools_mempid;

    int tot_chunks_alloc;
    int tot_chunks_inuse;
    int tot_chunks_partial;
    int tot_chunks_free;

    int tot_items_alloc;
    int tot_items_inuse;
    int tot_items_idle;

    int tot_overhead;
    ssize_t mem_idle_limit;
};

// 对外提供的宏接口，用来创建内存池
#define memPoolCreate MemPools::GetInstance().create

/* Allocator API */
/**
 \ingroup MemPoolsAPI
 * Initialise iteration through all of the pools.
 \retval  Iterator for use by memPoolIterateNext() and memPoolIterateDone()
 */
extern MemPoolIterator * memPoolIterate(void);

/**
 \ingroup MemPoolsAPI
 * Get next pool pointer, until getting NULL pointer.
 */
extern MemImplementingAllocator * memPoolIterateNext(MemPoolIterator * iter);

/**
 \ingroup MemPoolsAPI
 * Should be called after finished with iterating through all pools.
 */
extern void memPoolIterateDone(MemPoolIterator ** iter);

/**
 \ingroup MemPoolsAPI
 \todo Stats API - not sured how to refactor yet
 *
 * Fills MemPoolGlobalStats with statistical data about overall
 * usage for all pools.
 *
 \retval  Number of pools that have at least one object in use.
 *        Ie. number of dirty pools.
 */
extern int memPoolGetGlobalStats(MemPoolGlobalStats * stats);

/// \ingroup MemPoolsAPI
extern int memPoolInUseCount(MemAllocator *);
/// \ingroup MemPoolsAPI
extern int memPoolsTotalAllocated(void);

MemAllocatorProxy::MemAllocatorProxy(char const *aLabel, size_t const &aSize) : label (aLabel), size(aSize), theAllocator (NULL)
{
}


#endif /* _MEM_POOL_H_ */
