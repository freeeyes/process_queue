#pragma once

#include <string.h>
#include <cstdlib>
#include <stdio.h>
#include <cmath>
#include <sstream>
#include <thread>
#include <functional>

//自动判定操作系统
#define PLATFORM_WIN     0
#define PLATFORM_UNIX    1
#define PLATFORM_APPLE   2

#if defined(__WIN32__) || defined(WIN32) || defined(_WIN32) || defined(__WIN64__) || defined(WIN64) || defined(_WIN64)
#  define PSS_PLATFORM PLATFORM_WIN
#elif defined(__APPLE_CC__)
#  define PSS_PLATFORM PLATFORM_APPLE
#else
#  define PSS_PLATFORM PLATFORM_UNIX
#endif

#if PSS_PLATFORM == PLATFORM_WIN
#define key_t unsigned int
#endif

using queue_recv_message_func = std::function<void(const char*, size_t)>;

class CShm_queue_interface
{
public:
    virtual bool set_proc_message(const char* message_text, size_t len) = 0;
    virtual void recv_message(queue_recv_message_func fn_logic) = 0;
    virtual void close() = 0;
    virtual bool create_instance(key_t key, size_t message_size, int message_count) = 0;
    virtual void show_message_list() = 0;
    virtual std::string get_error() const = 0;
};
