
/*
 */


#include "config.h"
#if HAVE_ASSERT_H
#include <assert.h>
#endif

#include "MemPoolMalloc.h"

#if HAVE_STRING_H
#include <string.h>
#endif

/*
 * XXX This is a boundary violation between lib and src.. would be good
 * if it could be solved otherwise, but left for now.
 */
extern time_t squid_curtime;

void* MemPoolMalloc::allocate()
{
    void *obj = freelist.pop();
    if (obj) {
        memMeterDec(meter.idle);
        saved_calls++;
    } else {
        obj = xcalloc(1, obj_size);
        memMeterInc(meter.alloc);
    }
    memMeterInc(meter.inuse);
    return obj;
}

void MemPoolMalloc::deallocate(void *obj, bool aggressive)
{
    memMeterDec(meter.inuse);
    if (aggressive) {
        xfree(obj);
        memMeterDec(meter.alloc);
    } else {
        if (doZeroOnPush)
            memset(obj, 0, obj_size);
        memMeterInc(meter.idle);
        freelist.push_back(obj);
    }
}

/* TODO extract common logic to MemAllocate */
int MemPoolMalloc::getStats(MemPoolStats * stats, int accumulate)
{
    if (!accumulate)	/* need skip memset for GlobalStats accumulation */
        memset(stats, 0, sizeof(MemPoolStats));

    stats->pool = this;
    stats->label = objectType();
    stats->meter = &meter;
    stats->obj_size = obj_size;
    stats->chunk_capacity = 0;

    stats->chunks_alloc += 0;
    stats->chunks_inuse += 0;
    stats->chunks_partial += 0;
    stats->chunks_free += 0;

    stats->items_alloc += meter.alloc.level;
    stats->items_inuse += meter.inuse.level;
    stats->items_idle += meter.idle.level;

    stats->overhead += sizeof(MemPoolMalloc) + strlen(objectType()) + 1;

    return meter.inuse.level;
}

int MemPoolMalloc::getInUseCount()
{
    return meter.inuse.level;
}

MemPoolMalloc::MemPoolMalloc(char const *aLabel, size_t aSize) : MemImplementingAllocator(aLabel, aSize)
{
}

MemPoolMalloc::~MemPoolMalloc()
{
    assert(meter.inuse.level == 0 && "While trying to destroy pool");
    clean(0);
}

bool MemPoolMalloc::idleTrigger(int shift) const
{
    return freelist.count >> (shift ? 8 : 0);
}

void MemPoolMalloc::clean(time_t maxage)
{
    while (void *obj = freelist.pop()) {
        memMeterDec(meter.idle);
        memMeterDec(meter.alloc);
        xfree(obj);
    }
}

