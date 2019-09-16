/*
 * $Id$
 */
#ifndef _MEM_METER_H_
#define _MEM_METER_H_

#include "config.h"

/* 跟踪内存使用的每个动作的对象 (比如空闲对象 idle) */
class MemMeter
{
public:
    MemMeter() : level(0), hwater_level(0), hwater_stamp(0) {}
    ssize_t level;              /* current level (总数或者容量) */
    ssize_t hwater_level;       /* 高水位标记 */
    time_t hwater_stamp;        /* 上一次高水位标记改变的时间戳 */
};

#define memMeterSyncHWater(m)  { (m).hwater_level = (m).level; (m).hwater_stamp = squid_curtime ? squid_curtime : time(NULL); }
#define memMeterCheckHWater(m) { if ((m).hwater_level < (m).level) memMeterSyncHWater((m)); }
#define memMeterInc(m) { (m).level++; memMeterCheckHWater(m); }
#define memMeterDec(m) { (m).level--; }
#define memMeterAdd(m, sz) { (m).level += (sz); memMeterCheckHWater(m); }
#define memMeterDel(m, sz) { (m).level -= (sz); }

#endif /* _MEM_METER_H_ */
