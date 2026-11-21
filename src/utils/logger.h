#pragma once

#include <coreinit/debug.h>
#include <string.h>
#include <whb/log.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_APP_TYPE "P"
#define LOG_APP_NAME "StyleMiiU"

#define ANSI_RESET "\e[0m"

#define INFO_PREFIX    "\e[1;7;34m" "INFO"
#define WARN_PREFIX    "\e[1;7;33m" "WARN"
#define ERROR_PREFIX   "\e[1;7;31m" "ERROR"

#define __FILENAME__ ({                                \
    const char *__filename = __FILE__;                 \
    const char *__pos      = strrchr(__filename, '/'); \
    if (!__pos) __pos = strrchr(__filename, '\\');     \
    __pos ? __pos + 1 : __filename;                    \
})

#define LOG(LOG_FUNC, FMT, ...) \
    LOG_EX_DEFAULT(LOG_FUNC, "", "", FMT, __VA_ARGS__)

#define LOG_EX_DEFAULT(LOG_FUNC, LOG_LEVEL, LINE_END, FMT, ...) \
    LOG_EX(__FILENAME__, __FUNCTION__, __LINE__, LOG_FUNC, LOG_LEVEL, LINE_END, \
           FMT, __VA_ARGS__)

#define LOG_EX(FILENAME, FUNCTION, LINE, LOG_FUNC, LOG_LEVEL, LINE_END, FMT, ...)       \
    do {                                                                                \
    LOG_FUNC("[(%s)%18s][%23s]%30s@L%04d: " LOG_LEVEL ANSI_RESET " " FMT "" LINE_END,   \
             LOG_APP_TYPE, LOG_APP_NAME, FILENAME, FUNCTION, LINE __VA_OPT__(,)         \
             __VA_ARGS__);                                                              \
    } while (0)

#ifdef DEBUG

#ifdef VERBOSE_DEBUG

#define LOG_VERBOSE(...)                        \
    LOG(WHBLogPrintf, __VA_ARGS__)

#define LOG_VERBOSE_EX(FILENAME, FUNCTION, LINE, ...)                   \
    LOG_EX(FILENAME, FUNCTION, LINE, WHBLogPrintf, "", "", __VA_ARGS__)

#else // !VERBOSE_DEBUG

#define LOG_VERBOSE(...)                        \
    do {                                        \
        (void)sizeof((__VA_ARGS__));            \
    } while (0)
#define LOG_VERBOSE_EX(...)                     \
    do {                                        \
        (void)sizeof((__VA_ARGS__));            \
    } while (0)

#endif // !VERBOSE_DEBUG

#define LOG_ERROR(...)                                          \
    LOG_EX_DEFAULT(WHBLogPrintf, ERROR_PREFIX, "", __VA_ARGS__)
#define LOG_WARN(...)                                           \
    LOG_EX_DEFAULT(WHBLogPrintf, WARN_PREFIX, "", __VA_ARGS__)
#define LOG_INFO(...)                                           \
    LOG_EX_DEFAULT(WHBLogPrintf, INFO_PREFIX, "", __VA_ARGS__)

#define LOG_ERROR_LAMBDA(FILENAME, FUNCTION, LINE, ...)                   \
    LOG_EX(FILENAME, FUNCTION, LINE, WHBLogPrintf, ERROR_PREFIX, "", __VA_ARGS__);

#else // !DEBUG

#define LOG_VERBOSE_EX(...)                     \
    do {                                        \
        (void)sizeof((__VA_ARGS__));            \
    } while (0)

#define LOG_VERBOSE(...)                        \
    do {                                        \
        (void)sizeof((__VA_ARGS__));            \
    } while (0)

#define LOG_ERROR(...)                                          \
    LOG_EX_DEFAULT(OSReport, ERROR_PREFIX, "\n", __VA_ARGS__)

#define LOG_WARN(...)                                           \
    LOG_EX_DEFAULT(OSReport, WARN_PREFIX, "\n", __VA_ARGS__)

#define LOG_INFO(...)                                           \
    LOG_EX_DEFAULT(OSReport, INFO_PREFIX, "\n", __VA_ARGS__)

#define LOG_ERROR_LAMBDA(FILENAME, FUNCTION, LINE, ...)                                 \
    LOG_EX(FILENAME, FUNCTION, LINE, OSReport, ERROR_PREFIX, "\n", __VA_ARGS__);

#endif

void initLogging();

void deinitLogging();

#ifdef __cplusplus
}
#endif
