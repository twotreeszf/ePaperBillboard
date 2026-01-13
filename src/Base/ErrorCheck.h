#ifndef __ERROR_CHECK_H__
#define __ERROR_CHECK_H__

#include <assert.h>
#include "Logger.h"

#define ERR_CHECK_RET(cond)                                    \
    do                                                              \
    {                                                               \
        if ((cond) != true)                                         \
        {                                                           \
            LOG_E("Error check failed, return false: %s", #cond);   \
            return false;                                           \
        }                                                           \
    } while (0)

#define ERR_CHECK_FAIL(cond)                                   \
do                                                                  \
{                                                                   \
    if ((cond) != true)                                             \
    {                                                               \
        LOG_E("Error check failed, Assert false %s", #cond);        \
        assert(cond);                                               \
    }                                                               \
} while (0)

#endif /* __ERROR_CHECK_H__ */