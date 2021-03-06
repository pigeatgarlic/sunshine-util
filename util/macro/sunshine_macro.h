/**
 * @file sunshine_macro.h
 * @author {Do Huy Hoang} ({huyhoangdo0205@gmail.com})
 * @brief 
 * @version 1.0
 * @date 2022-07-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#ifndef __SUNSHINE_MACRO_H__
#define __SUNSHINE_MACRO_H__
#include <sunshine_log.h>


#define LOG_ERROR(content)  error::log(__FILE__,__LINE__,"error",content)

#define LOG_WARNING(content)  error::log(__FILE__,__LINE__,"warning",content)

#define LOG_DEBUG(content)  error::log(__FILE__,__LINE__,"debug",content)

// #define LOG_INFO(content)  error::log(__FILE__,__LINE__,"info",content)
#define LOG_INFO(content)  

#define LOG_OUTPUT_PIPE(content) printf(content)

#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

#define DO_NOTHING do_nothing

void do_nothing(void*);

bool string_compare(char* a, char* b);

#endif