#pragma once

// Assert
// ------
#if !defined(NDEBUG) && !defined(NPGS_ENABLE_ASSERT)
#define NPGS_ENABLE_ASSERT
#endif

#ifdef NPGS_ENABLE_ASSERT
#include <cstdlib>
#include <iostream>

#define NpgsAssert(Expr, ...)                                                                                 \
do {                                                                                                          \
if (!(Expr))                                                                                                  \
{                                                                                                             \
    std::cerr << "Assertion failed: " << #Expr << " in " << __FILE__ << " at line " << __LINE__ << std::endl; \
    __VA_OPT__(std::cerr << "Message: " << __VA_ARGS__ << std::endl;)                                          \
    std::abort();                                                                                             \
}                                                                                                             \
} while (false)

#else

#define NpgsAssert(Expr, ...)       static_cast<void>(0)
#endif // NPGS_ENABLE_ASSERT

#define NpgsStaticAssert(Expr, ...) static_assert(Expr __VA_OPT__(,) __VA_ARGS__)
