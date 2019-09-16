
/*老方式:
 *   在从堆栈中开辟空闲项到内存池中时，每个项目都需要调用xmalloc来开辟内存空间.
 *   每个项目都是系统调用malloc来开辟的，这会增加系统的(libmalloc)开销, 还有 
 *   在保留指向空闲项的指针链表时，增加了指向每个空闲项的指针的开销
 * 块组:
 *   xmalloc Chunk that fits at least MEM_MIN_FREE (32) items in an array, but
 *   limit Chunk size to MEM_CHUNK_MAX_SIZE (256K). Chunk size is rounded up to
 *   MEM_PAGE_SIZE (4K), trying to have chunks in multiples of VM_PAGE size.
 *   Minimum Chunk size is MEM_CHUNK_SIZE (16K).
 *   A number of items fits into a single chunk, depending on item size.
 *   Maximum number of items per chunk is limited to MEM_MAX_FREE (65535).
 *
 *   We populate Chunk with a linkedlist, each node at first word of item,
 *   and pointing at next free item. Chunk->FreeList is pointing at first
 *   free node. Thus we stuff free housekeeping into the Chunk itself, and
 *   omit pointer overhead per item.
 *
 *   Chunks are created on demand, and new chunks are inserted into linklist
 *   of chunks so that Chunks with smaller pointer value are placed closer
 *   to the linklist head. Head is a hotspot, servicing most of requests, so
 *   slow sorting occurs and Chunks in highest memory tend to become idle
 *   and freeable.
 *
 *   event is registered that runs every 15 secs and checks reference time
 *   of each idle chunk. If a chunk is not referenced for 15 secs, it is
 *   released.
 *
 *   [If mem_idle_limit is exceeded with pools, every chunk that becomes
 *   idle is immediately considered for release, unless this is the only
 *   chunk with free items in it.] (not implemented)
 *
 *   In cachemgr output, there are new columns for chunking. Special item,
 *   Frag, is shown to estimate approximately fragmentation of chunked
 *   pools. Fragmentation is calculated by taking amount of items in use,
 *   calculating needed amount of chunks to fit all, and then comparing to
 *   actual amount of chunks in use. Frag number, in percent, is showing
 *   how many percent of chunks are in use excessively. 100% meaning that
 *   twice the needed amount of chunks are in use.
 *   "part" item shows number of chunks partially filled. This shows how
 *   badly fragmentation is spread across all chunks.
 *
 *   Andres Kroonmaa.
 *   Copyright (c) 2003, Robert Collins <robertc@squid-cache.org>
 */

#include "config.h"
#if HAVE_ASSERT_H
#include <assert.h>
#endif

#include "MemPoolChunked.h"

#define MEM_MAX_MMAP_CHUNKS 2048

#if HAVE_STRING_H
#include <string.h>
#endif

/*
 * XXX This is a boundary violation between lib and src.. would be good
 * if it could be solved otherwise, but left for now.
 */
extern time_t squid_curtime;

static int memCompChunks(MemChunk* const &, MemChunk* const &);// 内存块比较
static int memCompObjChunks(void* const &, MemChunk* const &); // 对象比较

/* 内存块之间比较 */
static int memCompChunks(MemChunk* const &chunkA, MemChunk* const &chunkB)
{
    if (chunkA->objCache > chunkB->objCache)
        return 1;
    else if (chunkA->objCache < chunkB->objCache)
        return -1;
    else
        return 0;
}

/* Compare object to chunk */
static int memCompObjChunks(void* const &obj, MemChunk* const &chunk)
{
    /* 对象的内存地址低于块的地址区域 */
    if (obj < chunk->objCache)
        return -1;
    /* 对象所处的区域在内存池中 */
    if (obj < (void *) ((char *) chunk->objCache + chunk->pool->chunk_size))
        return 0;
    /* object is above the pool */
    return 1;
}

MemChunk::MemChunk(MemPoolChunked *aPool)
{
    /* 这也应该有一个内存池 
     * 请注意，这里要求:
     * - 给块池的第一个块分配一个块 
     * - 从池中分配一个块，将第一个块的内容移动到另一个块中，释放第一个块。 
     */
    inuse_count = 0;
    next = NULL;
    pool = aPool; // 内存池块
    
    /* 这里分配池中的第一块内存块块 */
    objCache = xcalloc(1, pool->chunk_size); // 在内存的动态存储区中分配num(num：对象个数)个长度为size(对象占据的内存字节数)的连续空间；并且初始为零.
    freeList = objCache;  // freeList 空闲链表头指针， objCache是新申请的内存区首地址
    void **Free = (void **)freeList;

    for (int i = 1; i < pool->chunk_capacity; i++) 
    {
        *Free = (void *) ((char *) Free + pool->obj_size);
        void **nextFree = (void **)*Free;
        (void) VALGRIND_MAKE_MEM_NOACCESS(Free, pool->obj_size);
        Free = nextFree;
    }

    nextFreeChunk = pool->nextFreeChunk;
    pool->nextFreeChunk = this;

    memMeterAdd(pool->getMeter().alloc, pool->chunk_capacity);
    memMeterAdd(pool->getMeter().idle, pool->chunk_capacity);
    pool->chunkCount++;
    
    lastref = squid_curtime;
    pool->allChunks.insert(this, memCompChunks);
}

MemPoolChunked::MemPoolChunked(const char *aLabel, size_t aSize) : MemImplementingAllocator(aLabel, aSize)
{
    chunk_size = 0;
    chunk_capacity = 0;
    chunkCount = 0;
    freeCache = 0;
    nextFreeChunk = 0;
    Chunks = 0;
    next = 0;

    setChunkSize(MEM_CHUNK_SIZE);// 8KB

#if HAVE_MALLOPT && M_MMAP_MAX
    mallopt(M_MMAP_MAX, MEM_MAX_MMAP_CHUNKS);
#endif
}

MemChunk::~MemChunk()
{
    memMeterDel(pool->getMeter().alloc, pool->chunk_capacity);
    memMeterDel(pool->getMeter().idle, pool->chunk_capacity);
    pool->chunkCount--;
    pool->allChunks.remove(this, memCompChunks);
    xfree(objCache);
}

// 把需要空闲的内存放入freeCache链表中
void MemPoolChunked::push(void *obj)
{
    void **Free;
    /*我们应该想出一个明智的方法来避免清除所有缓冲区。例如，membuf使用的数据缓冲区实际上不需要清除。
    这里有一个基于对象大小的条件，但是这样的条件不安全。*/
    if (doZeroOnPush)
        memset(obj, 0, obj_size); // obj空间设置为0
    Free = (void **)obj;
    *Free = freeCache;
    freeCache = obj;
    (void) VALGRIND_MAKE_MEM_NOACCESS(obj, obj_size);
}

/****************************************************************
 * 首先，找到一个空闲块。如果找不到根据需要创建新的内存块。
 * Insert new chunk in front of lowest ram chunk, making it preferred in future,
 * and resulting slow compaction towards lowest ram area.
 ***************************************************************/
void* MemPoolChunked::get()
{
    void **Free;

    saved_calls++;

    /*首先，如果空闲缓存 ，返回freeCache中第一个空闲块*/
    if (freeCache) { 
        Free = (void **)freeCache;
        (void) VALGRIND_MAKE_MEM_DEFINED(Free, obj_size);
        freeCache = *Free;
        *Free = NULL;
        return Free;
    }

    /* 内存池中查看有没有空闲内存 */
    if (nextFreeChunk == NULL) {
        /*每一个都没有, 创建一个新的内存块 */
        saved_calls--; // compensate for the ++ above
        createChunk();
    }

    /* 在内存块管理链中还有空闲的链表 */
    MemChunk *chunk = nextFreeChunk;

    Free = (void **)chunk->freeList;
    chunk->freeList = *Free;
    *Free = NULL;
    chunk->inuse_count++;
    chunk->lastref = squid_curtime;

    if (chunk->freeList == NULL) {
        /* nextFreeChunk不为空，说明下一个内存块还有空闲内存
        ，但是如果只空闲一块够使用了，那就让我们这块内存块的下一块内存块从内存块链表中摘出来 */
        nextFreeChunk = chunk->nextFreeChunk;
    }
    (void) VALGRIND_MAKE_MEM_DEFINED(Free, obj_size);
    return Free;
}

/* 创建出一块内存，然后把它放在内存块管理链表合适的位置，原则是地址小的在链表头，以此类推
   这里用创建好的内存块的首地址作为管理的节点，通过一个链表来管理这些节点 */
void MemPoolChunked::createChunk()
{
    MemChunk *chunk, *newChunk;

    newChunk = new MemChunk(this);

    chunk = Chunks;
    if (chunk == NULL) {	/* 内存池中的首个内存块 */
        Chunks = newChunk;  /* 内存块的首地址赋值给Chunks */
        return;
    }
    if (newChunk->objCache < chunk->objCache) { /* 如果不是内存池中的首个内存块 
         比较首个内存块首地址和新创建内存块首地址大小 
         如果新创建内存块首地址小于 首个内存块地址 那就把新创建的内存块
          作为首个内存块，原来首个内存块放在第二个上*/
        newChunk->next = chunk;
        Chunks = newChunk;
        return;
    }

    while (chunk->next) {
        if (newChunk->objCache < chunk->next->objCache) {
            /* 新内存块首地址小于Chunk下一个内存块首地址，插入 */
            newChunk->next = chunk->next;
            chunk->next = newChunk;
            return;
        }
        chunk = chunk->next;
    }
    /* 如果首地址链表中所有的节点都大于新创建内存块的首地址，那就插入到最后 */
    chunk->next = newChunk;
}

/* 设置块的大小 */ 
void MemPoolChunked::setChunkSize(size_t chunksize)
{
    int cap;
    size_t csize = chunksize; // 8196kb

    if (Chunks)		/* 篡改不安全？啥意思？ */
        return;
    // （8196+4096-1）/4096*4096 = 12287b = 12287字节
    csize = ((csize + MEM_PAGE_SIZE - 1) / MEM_PAGE_SIZE) * MEM_PAGE_SIZE;	/* 四舍五入到页大小 */
    // 12287字节 / 5字节 = 2457.4    
    cap = csize / obj_size; //计算出csize可以容纳多少个对象

    if (cap < MEM_MIN_FREE)
        cap = MEM_MIN_FREE;

    if (cap * obj_size > MEM_CHUNK_MAX_SIZE)
        cap = MEM_CHUNK_MAX_SIZE / obj_size;

    if (cap > MEM_MAX_FREE)
        cap = MEM_MAX_FREE;

    if (cap < 1)
        cap = 1;

    csize = cap * obj_size; // 最终计算出这块内存的大小
    csize = ((csize + MEM_PAGE_SIZE - 1) / MEM_PAGE_SIZE) * MEM_PAGE_SIZE;	/* 四舍五入到页大小 round up to page size */
    cap = csize / obj_size;

    chunk_capacity = cap; // 块容量
    chunk_size = csize;   // 块大小
}

/*
 * 警告：我们不会从池中清除此项，假设销毁只在程序结束时使用
 */
MemPoolChunked::~MemPoolChunked()
{
    MemChunk *chunk, *fchunk;

    flushMetersFull();
    clean(0);
    // 使用的内存还不为0 是强行终止程序
    assert(meter.inuse.level == 0 && "While trying to destroy pool");

    chunk = Chunks;
    while ( (fchunk = chunk) != NULL) {
        chunk = chunk->next;
        delete fchunk;
    }
    /* TODO 这里几个todo，我们应该对原始块指针做些什么? */

}

int MemPoolChunked::getInUseCount()
{
    return meter.inuse.level;
}

void* MemPoolChunked::allocate()
{
    void *p = get(); // 返回的是一个二级指针 void** Free;
    assert(meter.idle.level > 0);
    memMeterDec(meter.idle);
    memMeterInc(meter.inuse);
    return p;        //  到这里又返回的是一级指针
}

void MemPoolChunked::deallocate(void *obj, bool aggressive)
{
    push(obj);
    assert(meter.inuse.level > 0);
    memMeterDec(meter.inuse);
    memMeterInc(meter.idle);
}

void MemPoolChunked::convertFreeCacheToChunkFreeCache()
{
    void *Free;
    /*好的，所以我们必须遍历所有全局freecache，找到任意给定free所属的块，并将其填充到该块的freelist中*/

    while ((Free = freeCache) != NULL) {// 如果池中有空闲的内存
        MemChunk *chunk = NULL;
        chunk = const_cast<MemChunk *>(*allChunks.find(·, memCompObjChunks));
        assert(splayLastResult == 0);
        assert(chunk->inuse_count > 0);
        chunk->inuse_count--;
        (void) VALGRIND_MAKE_MEM_DEFINED(Free, sizeof(void *));
        freeCache = *(void **)Free;	/* 从全局freeCache中删除Free */
        *(void **)Free = chunk->freeList;	/* 插入chunks freelist */
        (void) VALGRIND_MAKE_MEM_NOACCESS(Free, sizeof(void *));
        chunk->freeList = Free;
        chunk->lastref = squid_curtime;
    }
}

/* 从内存池中移除空块 */
void MemPoolChunked::clean(time_t maxage)
{
    MemChunk *chunk, *freechunk, *listTail;
    time_t age;

    if (!this) // 内存池块不存在直接返回
        return;
    if (!Chunks) // 内存池块链表为空，直接返回
        return;

    flushMetersFull();
    convertFreeCacheToChunkFreeCache();
    /*现在我们把内存池里所有的东西都清理干净了，所有的空闲项目都释放返回给系统 */
    /*我们开始检查所有的内存块，看看是否可以释放 */
    /*从Chunks->next开始, 第一个chunk不释放 */
    /*从头开始重新构建nextFreeChunk链 */

    chunk = Chunks;
    while ((freechunk = chunk->next) != NULL) {
        age = squid_curtime - freechunk->lastref;
        freechunk->nextFreeChunk = NULL;
        if (freechunk->inuse_count == 0)
            if (age >= maxage) {
                chunk->next = freechunk->next;
                delete freechunk;
                freechunk = NULL;
            }
        if (chunk->next == NULL)
            break;
        chunk = chunk->next;
    }

    /*从头重新创建nextfreechunk列表 */
    /*按照使用量最多优先的顺序来重新建立nextFreeChunk链表*/
    /*如果使用量相等，但块首地址小于，则放在低地址处*/
    /*按创建时间来看第一个块总是在顶部，无论使用量是多少*/

    chunk = Chunks;
    nextFreeChunk = chunk;
    chunk->nextFreeChunk = NULL;

    while (chunk->next) {
        chunk->next->nextFreeChunk = NULL;
        if (chunk->next->inuse_count < chunk_capacity) 
        { // 使用的小于总容量
            listTail = nextFreeChunk;
            while (listTail->nextFreeChunk) {
                if (chunk->next->inuse_count > listTail->nextFreeChunk->inuse_count)
                    break;
                if ((chunk->next->inuse_count == listTail->nextFreeChunk->inuse_count) &&
                        (chunk->next->objCache < listTail->nextFreeChunk->objCache))
                    break;
                listTail = listTail->nextFreeChunk;
            }
            chunk->next->nextFreeChunk = listTail->nextFreeChunk;
            listTail->nextFreeChunk = chunk->next;
        }
        chunk = chunk->next;
    }
    /*如果第一个块使用完了，就移除第一块，从第二个块开始*/
    if (nextFreeChunk->inuse_count == chunk_capacity)
        nextFreeChunk = nextFreeChunk->nextFreeChunk;

    return;
}

bool MemPoolChunked::idleTrigger(int shift) const
{
    return meter.idle.level > (chunk_capacity << shift);
}

/*给这个池单利更新 MemPoolStats结构体*/
int MemPoolChunked::getStats(MemPoolStats* stats, int accumulate)
{
    MemChunk *chunk;
    int chunks_free = 0;
    int chunks_partial = 0;

    if (!accumulate)	/*第一次 accumulate 应该是 true，之后需要跳过，统计是一个累计值*/
        memset(stats, 0, sizeof(MemPoolStats));

    clean((time_t) 555555);	/*在上报之前不释内存放块*/

    stats->pool = this;
    stats->label = objectType();
    stats->meter = &meter;
    stats->obj_size = obj_size;
    stats->chunk_capacity = chunk_capacity;

    /*统计每一个块的使用和空闲情况*/
    chunk = Chunks;
    while (chunk) {
        if (chunk->inuse_count == 0)
            chunks_free++;
        else if (chunk->inuse_count < chunk_capacity)
            chunks_partial++;
        chunk = chunk->next;
    }

    stats->chunks_alloc += chunkCount;
    stats->chunks_inuse += chunkCount - chunks_free;
    stats->chunks_partial += chunks_partial;
    stats->chunks_free += chunks_free;

    stats->items_alloc += meter.alloc.level;
    stats->items_inuse += meter.inuse.level;
    stats->items_idle += meter.idle.level;

    stats->overhead += sizeof(MemPoolChunked) + chunkCount * sizeof(MemChunk) + strlen(objectType()) + 1;

    return meter.inuse.level;
}

