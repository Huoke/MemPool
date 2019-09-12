#ifndef ASSERT_H
#define ASSERT_H

#define assert(EX)  ((EX)?((void)0):xassert("EX", __FILE__, __LINE__))

#ifdef __cplusplus
extern "C" void xassert(const char *, const char *, int);
#endif /*ASSERT_H*/