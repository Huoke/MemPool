/*********************************************
 * 采用vector来实现栈，帮助伸展树利用栈进行遍历
 ********************************************/

#ifndef STACK_H
#define STACK_H

#include "Array.h"


template <class S = void *>

class Stack : public Vector<S>
{
public:
    using Vector<S>::count;
    using Vector<S>::items;
    typedef typename Vector<S>::value_type value_type;
    typedef typename Vector<S>::pointer pointer;
    value_type pop() {
        if (!count)
            return value_type();

        value_type result = items[--count];

        this->items[count] = value_type();

        return result;
    }

    value_type top() const {
        return count ? items[count - 1] : value_type();
    }
};

#endif /* STACK_H */
