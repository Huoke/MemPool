/*
 * 参照STLVector实现Array
 */
#ifndef ARRAY_H
#define ARRAY_H

#include "fatal.h"
#include "util.h"

#include "compat/assert.h"

/* 支持迭代器 */
template <class C>
class VectorIteratorBase
{
public:
    VectorIteratorBase();
    VectorIteratorBase(C &);
    VectorIteratorBase(size_t, C &);
    VectorIteratorBase & operator =(VectorIteratorBase const &);
    bool operator != (VectorIteratorBase const &rhs);
    bool operator == (VectorIteratorBase const &rhs);
    VectorIteratorBase & operator ++();
    VectorIteratorBase operator ++(int);
    typename C::value_type & operator *() const {
        return theVector->items[pos];
    }

    typename C::value_type * operator -> () const {
        return &theVector->items[pos];
    }

    ssize_t operator - (VectorIteratorBase const &rhs) const;
    bool incrementable() const;

private:
    size_t pos;
    C * theVector;
};

template<class E>
class Vector
{

public:
    typedef E value_type;
    typedef E* pointer;
    typedef VectorIteratorBase<Vector<E> > iterator;
    typedef VectorIteratorBase<Vector<E> const> const_iterator;

    void *operator new (size_t);
    void operator delete (void *);

    Vector();
    ~Vector();
    Vector(Vector const &);
    Vector &operator = (Vector const &);
    void clean();
    void reserve (size_t capacity);
    void push_back (E);
    Vector &operator += (E item) {push_back(item); return *this;};

    void insert (E);
    E &back();
    E pop_back();
    E shift();         // 弹出最前面的元素
    void prune(E);
    void preAppend(int app_count);
    bool empty() const;
    size_t size() const;
    iterator begin();
    const_iterator begin () const;
    iterator end();
    const_iterator end () const;
    E& operator [] (unsigned i);
    const E& operator [] (unsigned i) const;

    size_t capacity;
    size_t count;
    E *items;
};

template<class E>
void* Vector<E>::operator new(size_t size)
{
    return xmalloc(size);
}

template<class E>
void Vector<E>::operator delete (void *address)
{
    xfree (address);
}

template<class E>
Vector<E>::Vector() : capacity (0), count(0), items (NULL)
{}

template<class E>
Vector<E>::~Vector()
{
    clean();
}

template<class E>
void Vector<E>::clean()
{
    delete[] items;
    items = NULL;
    capacity = 0;
    count = 0;
}

template<class E>
void Vector<E>::reserve(size_t min_capacity)
{
    const int min_delta = 16;
    int delta;

    if (capacity >= min_capacity)
        return;

    delta = min_capacity;

    delta += min_delta - 1;

    delta /= min_delta;

    delta *= min_delta;

    if (delta < 0)
        delta = min_capacity - capacity;

    E*newitems = new E[capacity + delta];

    for (size_t counter = 0; counter < size(); ++counter) {
        newitems[counter] = items[counter];
    }

    capacity += delta;
    delete[]items;
    items = newitems;
}

template<class E>
void Vector<E>::push_back(E obj)
{
    if (size() >= capacity)
        reserve (size() + 1);

    items[count++] = obj;
}

template<class E>
void Vector<E>::insert(E obj)
{
    if (size() >= capacity)
        reserve (size() + 1);

    int i;

    for (i = count; i > 0; i--)
        items[i] = items[i - 1];

    items[i] = obj;

    count += 1;
}

template<class E>
E Vector<E>::shift()
{
    assert (size());
    value_type result = items[0];

    for (unsigned int i = 1; i < count; i++)
        items[i-1] = items[i];

    count--;

    return result;
}

template<class E>
E Vector<E>::pop_back()
{
    assert (size());
    value_type result = items[--count];
    items[count] = value_type();
    return result;
}

template<class E>
E& Vector<E>::back()
{
    assert (size());
    return items[size() - 1];
}

template<class E>
void Vector<E>::prune(E item)
{
    unsigned int n = 0;
    for (unsigned int i = 0; i < count; i++) {
        if (items[i] != item) {
            if (i != n)
                items[n] = items[i];
            n++;
        }
    }

    count = n;
}

template<class E>
void Vector<E>::preAppend(int app_count)
{
    if (size() + app_count > capacity)
        reserve(size() + app_count);
}

template<class E>
Vector<E>::Vector (Vector<E> const &rhs)
{
    items = NULL;
    capacity = 0;
    count = 0;
    reserve (rhs.size());

    for (size_t counter = 0; counter < rhs.size(); ++counter)
        push_back (rhs.items[counter]);
}

template<class E>
Vector<E>& Vector<E>::operator = (Vector<E> const &old)
{
    clean();
    reserve (old.size());

    for (size_t counter = 0; counter < old.size(); ++counter)
        push_back (old.items[counter]);

    return *this;
}

template<class E>
bool Vector<E>::empty() const
{
    return size() == 0;
}

template<class E>
size_t Vector<E>::size() const
{
    return count;
}

template<class E>
typename Vector<E>::iterator Vector<E>::begin()
{
    return iterator (0, *this);
}

template<class E>
typename Vector<E>::iterator Vector<E>::end()
{
    return iterator(size(), *this);
}

template<class E>
typename Vector<E>::const_iterator Vector<E>::begin() const
{
    return const_iterator (0, *this);
}

template<class E>
typename Vector<E>::const_iterator Vector<E>::end() const
{
    return const_iterator(size(), *this);
}

template<class E>
E& Vector<E>::operator [] (unsigned i)
{
    assert (size() > i);
    return items[i];
}

template<class E>
const E& Vector<E>::operator [] (unsigned i) const
{
    assert (size() > i);
    return items[i];
}

template<class C>
VectorIteratorBase<C>::VectorIteratorBase() : pos(0), theVector(NULL)
{}

template<class C>
VectorIteratorBase<C>::VectorIteratorBase(C &container) : pos(container.begin()), theVector(&container)
{}

template<class C>
VectorIteratorBase<C>::VectorIteratorBase(size_t aPos, C &container) : pos(aPos), theVector(&container) {}

template<class C>
bool VectorIteratorBase<C>:: operator != (VectorIteratorBase<C> const &rhs)
{
    assert (theVector);
    return pos != rhs.pos;
}

template<class C>
bool VectorIteratorBase<C>:: operator == (VectorIteratorBase<C> const &rhs)
{
    assert (theVector);
    return pos == rhs.pos;
}

template<class C>
bool VectorIteratorBase<C>::incrementable() const
{
    assert (theVector);
    return pos != theVector->size();
}

template<class C>
VectorIteratorBase<C> & VectorIteratorBase<C>:: operator ++()
{
    assert (theVector);

    if (!incrementable())
        fatal ("domain error");

    ++pos;

    return *this;
}

template<class C>
VectorIteratorBase<C> VectorIteratorBase<C>:: operator ++(int)
{
    VectorIteratorBase result(*this);
    ++*this;
    return result;
}

template<class C>
VectorIteratorBase<C>&
VectorIteratorBase<C>::operator =(VectorIteratorBase<C> const &old)
{
    pos = old.pos;
    theVector = old.theVector;
    return *this;
}

template<class C>
ssize_t VectorIteratorBase<C>::operator - (VectorIteratorBase<C> const &rhs) const
{
    assert(theVector == rhs.theVector);
    return pos - rhs.pos;
}

#endif /* ARRAY_H */
