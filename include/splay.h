/***********************************************************************
 * 伸展树实现
 ***********************************************************************/
#ifndef SPLAY_H
#define SPLAY_H

#ifndef __cplusplus
#else

#include "Stack.h"

template <class V>
class SplayNode
{
public:
    typedef V Value;
    typedef int SPLAYCMP(Value const &a, Value const &b);
    typedef void SPLAYFREE(Value &);
    typedef void SPLAYWALKEE(Value const & nodedata, void *state);
    static void DefaultFree (Value &aValue) {delete aValue;}

    SplayNode<V> (Value const &);
    Value data;
    mutable SplayNode<V> *left;
    mutable SplayNode<V> *right;
    void destroy(SPLAYFREE *);
    void walk(SPLAYWALKEE *, void *callerState);
    bool empty() const { return this == NULL; }
    SplayNode<V> const * start() const;
    SplayNode<V> const * finish() const;

    SplayNode<V> * remove
    (const Value data, SPLAYCMP * compare);

    SplayNode<V> * insert(Value data, SPLAYCMP * compare);

    template <class FindValue> SplayNode<V> * splay(const FindValue &data, int( * compare)(FindValue const &a, Value const &b)) const;
};

typedef SplayNode<void *> splayNode;

template <class V>

class SplayConstIterator;

template <class V>

class SplayIterator;

// 模板类
template <class V>
class Splay
{
public:
    typedef V Value;
    typedef int SPLAYCMP(Value const &a, Value const &b);
    typedef void SPLAYFREE(Value &);
    typedef SplayIterator<V> iterator;
    typedef const SplayConstIterator<V> const_iterator;
    Splay():head(NULL), elements (0) {}

    mutable SplayNode<V> * head;
    template <class FindValue> Value const *find (FindValue const &, int( * compare)(FindValue const &a, Value const &b)) const;
    void insert(Value const &, SPLAYCMP *compare);

    void remove
    (Value const &, SPLAYCMP *compare);

    void destroy(SPLAYFREE *);

    SplayNode<V> const * start() const;

    SplayNode<V> const * finish() const;

    size_t size() const;

    const_iterator begin() const;

    const_iterator end() const;

    size_t elements;
};


extern "C" int splayLastResult;

extern "C" splayNode *splay_insert(void *, splayNode *, splayNode::SPLAYCMP *);

extern "C" splayNode *splay_delete(const void *, splayNode *, splayNode::SPLAYCMP *);

extern "C" splayNode *splay_splay(const void **, splayNode *, splayNode::SPLAYCMP *);

extern "C" void splay_destroy(splayNode *, splayNode::SPLAYFREE *);

extern "C" void splay_walk(splayNode *, splayNode::SPLAYWALKEE *, void *callerState);

/* 内联方法 */
template<class V>
SplayNode<V>::SplayNode (Value const &someData) : data(someData), left(NULL), right (NULL) {}

template<class V>
void SplayNode<V>::walk(SPLAYWALKEE * walkee, void *state)
{
    if (this == NULL)
        return;

    if (left)
        left->walk(walkee, state);

    walkee(data, state);

    if (right)
        right->walk(walkee, state);
}

template<class V>
SplayNode<V> const* SplayNode<V>::start() const
{
    if (this && left)
        return left->start();

    return this;
}

template<class V>
SplayNode<V> const* SplayNode<V>::finish() const
{
    if (this && right)
        return right->finish();

    return this;
}

template<class V>
void SplayNode<V>::destroy(SPLAYFREE * free_func)
{
    if (!this)
        return;

    if (left)
        left->destroy(free_func);

    if (right)
        right->destroy(free_func);

    free_func(data);

    delete this;
}

template<class V>
SplayNode<V>* SplayNode<V>::remove(Value const dataToRemove, SPLAYCMP * compare)
{
    if (this == NULL)
        return NULL;

    SplayNode<V> *result = splay(dataToRemove, compare);

    if (splayLastResult == 0) {	/* 找到 */
        SplayNode<V> *newTop;

        if (result->left == NULL) {
            newTop = result->right;
        } else {
            newTop = result->left->splay(dataToRemove, compare);
            /* temporary */
            newTop->right = result->right;
            result->right = NULL;
        }

        delete result;
        return newTop;
    }

    return result;		
}

template<class V>
SplayNode<V> *
SplayNode<V>::insert(Value dataToInsert, SPLAYCMP * compare)
{
    /* 创建一个节点，并插入到树中 */
    SplayNode<V> *newNode = new SplayNode<V>(dataToInsert);

    if (this == NULL) {
        splayLastResult = -1;
        newNode->left = newNode->right = NULL;
        return newNode;
    }

    SplayNode<V> *newTop = splay(dataToInsert, compare);

    if (splayLastResult < 0) {
        newNode->left = newTop->left;
        newNode->right = newTop;
        newTop->left = NULL;
        return newNode;
    } else if (splayLastResult > 0) {
        newNode->right = newTop->right;
        newNode->left = newTop;
        newTop->right = NULL;
        return newNode;
    } else {
        /* duplicate entry */
        delete newNode;
        return newTop;
    }
}

template<class V>
template<class FindValue>
SplayNode<V>* SplayNode<V>::splay(FindValue const &dataToFind, int( * compare)(FindValue const &a, Value const &b)) const
{
    if (this == NULL) {
        /* 不可能比较成功 */
        splayLastResult = -1;
        return NULL;
    }

    Value temp = Value();
    SplayNode<V> N(temp);
    SplayNode<V> *l;
    SplayNode<V> *r;
    SplayNode<V> *y;
    N.left = N.right = NULL;
    l = r = &N;

    SplayNode<V> *top = const_cast<SplayNode<V> *>(this);

    for (;;) {
        splayLastResult = compare(dataToFind, top->data);

        if (splayLastResult < 0) {
            if (top->left == NULL)
                break;

            if ((splayLastResult = compare(dataToFind, top->left->data)) < 0) {
                y = top->left;	/* 向右旋转 */
                top->left = y->right;
                y->right = top;
                top = y;

                if (top->left == NULL)
                    break;
            }

            r->left = top;	/* 连接右字节点 */
            r = top;
            top = top->left;
        } else if (splayLastResult > 0) {
            if (top->right == NULL)
                break;

            if ((splayLastResult = compare(dataToFind, top->right->data)) > 0) {
                y = top->right;	/* 向左旋转 */
                top->right = y->left;
                y->left = top;
                top = y;

                if (top->right == NULL)
                    break;
            }

            l->right = top;	/* 连接左子节点 */
            l = top;
            top = top->right;
        } else {
            break;
        }
    }

    l->right = top->left;	
    r->left = top->right;
    top->left = N.right;
    top->right = N.left;
    return top;
}

template <class V>
template <class FindValue>
typename Splay<V>::Value const* Splay<V>::find (FindValue const &value, int( * compare)(FindValue const &a, Value const &b)) const
{
    head = head->splay(value, compare);

    if (splayLastResult != 0)
        return NULL;

    return &head->data;
}

template <class V>
void Splay<V>::insert(Value const &value, SPLAYCMP *compare)
{
    assert (!find (value, compare));
    head = head->insert(value, compare);
    ++elements;
}

template <class V>
void Splay<V>::remove(Value const &value, SPLAYCMP *compare)
{
    assert (find (value, compare));

    head = head->remove
           (value, compare);

    --elements;
}

template <class V>
SplayNode<V> const* Splay<V>:: start() const
{
    if (head)
        return head->start();

    return NULL;
}

template <class V>
SplayNode<V> const* Splay<V>:: finish() const
{
    if (head)
        return head->finish();

    return NULL;
}

template <class V>
void Splay<V>:: destroy(SPLAYFREE *free_func)
{
    if (head)
        head->destroy(free_func);

    head = NULL;
    elements = 0;
}

template <class V>
size_t Splay<V>::size() const
{
    return elements;
}

template <class V>
const SplayConstIterator<V> Splay<V>::begin() const
{
    return const_iterator(head);
}

template <class V>
const SplayConstIterator<V> Splay<V>::end() const
{
    return const_iterator(NULL);
}

template <class V>
class SplayConstIterator
{

public:
    typedef const V value_type;
    SplayConstIterator (SplayNode<V> *aNode);
    bool operator == (SplayConstIterator const &right) const;
    SplayConstIterator operator ++ (int dummy);
    SplayConstIterator &operator ++ ();
    V const & operator * () const;

private:
    void advance();
    void addLeftPath(SplayNode<V> *aNode);
    void init(SplayNode<V> *);
    Stack<SplayNode<V> *> toVisit;
};

template <class V>
SplayConstIterator<V>::SplayConstIterator (SplayNode<V> *aNode)
{
    init(aNode);
}

template <class V>
bool SplayConstIterator<V>::operator == (SplayConstIterator const &right) const
{
    return toVisit.top() == right.toVisit.top();
}

template <class V>
SplayConstIterator<V>& SplayConstIterator<V>::operator ++ ()
{
    advance();
    return *this;
}

template <class V>
SplayConstIterator<V> SplayConstIterator<V>::operator ++ (int dummy)
{
    SplayConstIterator<V> result = *this;
    advance();
    return result;
}
/***********************************************
* 前进很简单：
* 如果堆栈是空的，我们就完了。
* 否则，弹出上次访问的节点
* 然后，弹出下一个要访问的节点
* 如果它有一个右子项，添加它，它是从左到尾的路径。
* 然后将节点添加回。
***********************************************/
template <class V>
void SplayConstIterator<V>::advance()
{
    if (toVisit.size() == 0)
        return;

    toVisit.pop();

    if (toVisit.size() == 0)
        return;

    SplayNode<V> *currentNode = toVisit.pop();

    addLeftPath(currentNode->right);

    toVisit.push_back(currentNode);
}

template <class V>
void SplayConstIterator<V>::addLeftPath(SplayNode<V> *aNode)
{
    if (aNode == NULL)
        return;

    do {
        toVisit.push_back(aNode);
        aNode = aNode->left;
    } while (aNode != NULL);
}

template <class V>
void SplayConstIterator<V>::init(SplayNode<V> *head)
{
    addLeftPath(head);
}

template <class V>
V const& SplayConstIterator<V>::operator * () const
{
    if (toVisit.size() == 0)
        fatal ("Attempt to dereference SplayConstIterator past-the-end\n");

    return toVisit.top()->data;
}

#endif /* cplusplus */

#endif /* SPLAY_H */
