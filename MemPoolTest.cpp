#include "MemPool.h"
#include <iostream>


void xassert(const char *msg, const char *file, int line)
{
    std::cout << "Assertion failed: (" << msg << ") at " << file << ":" << line << std::endl;
    exit (1);
}

class MemPoolTest
{
public:
    void run();
private:
    class SomethingToAlloc
    {
    public:
        int aValue;
    };
    static MemAllocator *Pool; // 静态内存分配器
};

MemAllocator *MemPoolTest::Pool = NULL;

void MemPoolTest::run()
{
    assert (Pool == NULL);
    // 调用 MemPools::GetInstance().create 单利方法
    Pool = memPoolCreate("Test Pool", sizeof(SomethingToAlloc));
    assert (Pool);
    
    SomethingToAlloc *something = static_cast<SomethingToAlloc *>(Pool->alloc());
    assert (something);
    assert (something->aValue == 0);
    something->aValue = 5;
    Pool->free(something);
    SomethingToAlloc *otherthing = static_cast<SomethingToAlloc *>(Pool->alloc());
    assert (otherthing == something);
    assert (otherthing->aValue == 0);
    Pool->free (otherthing);
    delete Pool;
}


int main (int argc, char **argv)
{
    MemPoolTest aTest;
    aTest.run();
    return 0;
}

