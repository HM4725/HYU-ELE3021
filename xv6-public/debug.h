#define DEBUG_LEVEL_ALL    0
#define DEBUG_LEVEL_TRACE  1
#define DEBUG_LEVEL_DEBUG  2
#define DEBUG_LEVEL_INFO   3
#define DEBUG_LEVEL_WARN   4
#define DEBUG_LEVEL_ERROR  5

#define LOGGING            1
#define LOG_LEVEL          DEBUG_LEVEL_TRACE

#define TO_STR2(arg) #arg
#define TO_STR(arg) TO_STR2(arg)

#if LOGGING && LOG_LEVEL <= DEBUG_LEVEL_TRACE
#define kprintf_trace(...) cprintf(__FILE__ ":" TO_STR(__LINE__) " [TRACE] " __VA_ARGS__)
#else
#define kprintf_trace(...)
#endif

#if LOGGING && LOG_LEVEL <= DEBUG_LEVEL_DEBUG
#define kprintf_debug(...) cprintf(__FILE__ ":" TO_STR(__LINE__) " [DEBUG] " __VA_ARGS__)
#else
#define kprintf_debug(...)
#endif

#if LOGGING && LOG_LEVEL <= DEBUG_LEVEL_INFO
#define kprintf_info(...) cprintf(__FILE__ ":" TO_STR(__LINE__) " [INFO] " __VA_ARGS__)
#else
#define kprintf_info(...)
#endif

#if LOGGING && LOG_LEVEL <= DEBUG_LEVEL_WARN
#define kprintf_warn(...) cprintf(__FILE__ ":" TO_STR(__LINE__) " [WARN] " __VA_ARGS__)
#else
#define kprintf_warn(...)
#endif

#if LOGGING && LOG_LEVEL <= DEBUG_LEVEL_ERROR
#define kprintf_error(...) cprintf(__FILE__ ":" TO_STR(__LINE__) " [ERROR] " __VA_ARGS__)
#else
#define kprintf_error(...)
#endif
