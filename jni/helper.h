/*
file: helper.h
author: skeu
description: 辅助函数接口
*/
#ifndef HELPER_H
#define HELPER_H
#include <stdarg.h>
class MemWriter;

void cache_flush(uint32_t begin, uint32_t end);
void hexdump(void *mem, unsigned int len);
int compress_data (uint8_t const *data, size_t size, MemWriter *out);

class MemWriter {
public:
    MemWriter (size_t size = 1024) {
        this->buf = new uint8_t[size];
        this->len = 0;
        this->max = size;
    }
    ~MemWriter () {
        delete[] this->buf;
    }
    inline uint8_t *getBuffPtr () {
        return this->buf;
    }
    inline size_t getBuffSize () {
        return this->len;
    }
    void clear ();
    void write (uint8_t const *data, size_t len);
    bool sprintf (char const *fmt, ...);
    bool vsprintf (char const *fmt, va_list arg);
private:
    void checkSize (size_t len);
    uint8_t *buf;
    size_t len;
    size_t max;
};

#endif