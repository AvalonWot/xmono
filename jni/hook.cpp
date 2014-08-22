/*
file: hook.h
author: skeu
description: arm架构下 hook
*/
#include <stdint.h>
#include <string.h>
#include <deque>
#include <pthread.h>
#include <errno.h>
#include <sys/mman.h>
#include <android/log.h>
#include "hook.h"
#include "helper.h"

#define LOG_TAG "XMONODEBUG"
#define LOGD(fmt, args...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG, fmt, ##args)
#define LOGE(fmt, args...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG, fmt, ##args)

static std::deque<char*> mem_cache;
static pthread_mutex_t alloc_mutex = PTHREAD_MUTEX_INITIALIZER;
static char *alloc_trampo () {
    char *r;
    pthread_mutex_lock (&alloc_mutex);
    if (mem_cache.empty()) {
        LOGD ("alloc_trampo's memory cache...");
        size_t map_size = 10240 * 16;
        char *p = (char*)mmap (0, map_size, PROT_EXEC | PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (p == MAP_FAILED) {
            LOGD ("mmap error : %s", strerror (errno));
            r = 0;
            goto end;
        }
        for (int i = 0; i < map_size / 16; i++) {
            mem_cache.push_back (p + i * 16);
        }
        LOGD ("alloc_trampo's memory cache end...");
    }
    r = mem_cache.front ();
    mem_cache.pop_front ();
end:
    pthread_mutex_unlock (&alloc_mutex);
    return r;
}

static void emit_arm_jmp (void *_buf, void *_dst) {
    char *buf = (char*)_buf;
    uint32_t dst = (uint32_t)_dst;
    uint8_t jmp_code[] = {0x04, 0xF0, 0x1F, 0xE5};      /*ldr pc, [pc, #-4]*/
    memcpy (buf, &jmp_code, 4);
    memcpy (buf + 4, &dst, 4);
}

int arm_hook (void *_org, void *dst, void **trampo) {
    char *org = (char*)_org;
    if (!org) return 0;
    if (mprotect ((void*)((uint32_t)org & ~(PAGE_SIZE - 1)), 8, PROT_EXEC | PROT_WRITE | PROT_READ) != 0) {
        LOGD ("mprotect error : %s", strerror (errno));
        return 0;
    }
    char *tr = alloc_trampo ();
    if (tr == 0)
        return 0;
    memcpy (tr, org, 8);        /*提取原函数的前两个指令*/
    emit_arm_jmp (tr + 8, org + 8);
    cache_flush ((uint32_t)tr, (uint32_t)(tr + 16));
    *trampo = tr;
    emit_arm_jmp (org, dst);    /*修改原函数头*/
    cache_flush ((uint32_t)org, (uint32_t)(org + 8));
    return 1;
}