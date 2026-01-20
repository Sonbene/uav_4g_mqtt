/**
 * @file    debug_log.h
 * @brief   Debug Logging Module using UART1
 */

#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stdbool.h>
#include "main.h"

/* GLOBAL DEBUG ENABLE SWITCH 
 * Comment out this line to disable all debug logging code generation
 */
/* #define DEBUG_ENABLE */  /* DISABLED: UART1 is used for MAVLink! */

#ifdef DEBUG_ENABLE
    
    /**
     * @brief Initialize Debug UART (UART1)
     * @return true if successful
     */
    void Debug_Init(void);
    
    /**
     * @brief Print formatted debug message
     * @param fmt Format string
     * @param ... Arguments
     */
    void Debug_Log(const char *fmt, ...);
    
    /* Log wrapper macros - easy to expand later with levels/colors if needed */
    #define LOG_INFO(fmt, ...)  Debug_Log("[INFO] " fmt "\r\n", ##__VA_ARGS__)
    #define LOG_WARN(fmt, ...)  Debug_Log("[WARN] " fmt "\r\n", ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) Debug_Log("[ERROR] " fmt "\r\n", ##__VA_ARGS__)
    #define LOG_RAW(fmt, ...)   Debug_Log(fmt, ##__VA_ARGS__)

#else
    /* Empty macros when disabled - code optimizes away */
    #define Debug_Init()        ((void)0)
    #define Debug_Log(fmt, ...) ((void)0)
    
    #define LOG_INFO(fmt, ...)  ((void)0)
    #define LOG_WARN(fmt, ...)  ((void)0)
    #define LOG_ERROR(fmt, ...) ((void)0)
    #define LOG_RAW(fmt, ...)   ((void)0)

#endif /* DEBUG_ENABLE */

#endif /* DEBUG_LOG_H */
