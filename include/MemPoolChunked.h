#ifndef _MEM_POOL_CHUNKED_H_
#define _MEM_POOL_CHUNKED_H_

#include "MemPool.h"

/// \ingroup MemPoolsAPI
#define MEM_PAGE_SIZE 4096      // 默认设定的页大小为4Kb，也就是4096字节
/// \ingroup MemPoolsAPI
#define MEM_CHUNK_SIZE 4096 * 4 // 块大小为16kb， 也就是 16402 字节
/// \ingroup MemPoolsAPI
#define MEM_CHUNK_MAX_SIZE  256 * 1024	/*  2MB ，这注释是有问题啊？*/
/// \ingroup MemPoolsAPI
#define MEM_MIN_FREE  32
/// \ingroup MemPoolsAPI
#define MEM_MAX_FREE  65535	/* ushort is max number of items per chunk */

class MemChunk;

class MemPoolChunked : public MemImplementingAllocator
{
public:
    friend class MemChunk;
    MemPoolChunked(const char *label, size_t obj_size);
    ~MemPoolChunked();
    void convertFreeCacheToChunkFreeCache();
    virtual void clean(time_t maxage);

    /**
     \param stats	Object to be filled with statistical data about pool.
     \retval		Number of objects in use, ie. allocated.
     */
    virtual int getStats(MemPoolStats * stats, int accumulate);

    void createChunk();
    void *get();
    void push(void *obj);
    virtual int getInUseCount();
protected:
    virtual void *allocate();
    virtual void deallocate(void *, bool aggressive);
public:
    /**
     * 允许调整内存池块的大小。
     * 对象是按照分块来分配的，而不是单独分配的。
     * 这样可以节省内存，减少碎片，所以内存也只能以块的形式释放。
     * 因此，内存保护需要在分块和内存碎片之间权衡 Therefore there is tradeoff between memory conservation due to chunking and free memory fragmentation.
     * 注意: 一个原则，增加块的大小 仅仅对保存很多项但是存在时间较长的内存池而言 As a general guideline, increase chunk size only for pools that keep
     *      very many items for relatively long time.
     */

    virtual void setChunkSize(size_t chunksize);

    virtual bool idleTrigger(int shift) const;

    size_t chunk_size;  // 块大小
    int chunk_capacity; // 块容量
    int memPID;         // 内存id
    int chunkCount;     // 块个数
    void *freeCache;    // 释放的缓存
    MemChunk *nextFreeChunk; // 下一个空闲块
    MemChunk *Chunks;        // 块
    Splay<MemChunk *> allChunks; // 所有块
};

/* 内存块类是对内存块数据结构的抽象 */
class MemChunk
{
public:
    MemChunk(MemPoolChunked *pool);
    ~MemChunk();
    void *freeList;  // 
    void *objCache;  //
    int inuse_count; // 
    MemChunk *nextFreeChunk; // 下一个需要释放的块
    MemChunk *next;    // 下一个内存块
    time_t lastref;
    MemPoolChunked *pool; // 内存池块
};

#endif /* _MEM_POOL_CHUNKED_H_ */
