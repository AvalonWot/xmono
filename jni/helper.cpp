/*
file: alice.cpp
author: skeu
description: 辅助函数实现
*/
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <zlib.h>
#include <android/log.h>
#include "helper.h"

#define LOG_TAG "XMONODEBUG"
#define LOGD(fmt, args...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG, fmt, ##args)
#define LOGE(fmt, args...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG, fmt, ##args)

void cache_flush(uint32_t begin, uint32_t end)
{   
    const int syscall = 0xf0002;
    __asm __volatile (
        "mov     r0, %0\n"          
        "mov     r1, %1\n"
        "mov     r7, %2\n"
        "mov     r2, #0x0\n"
        "svc     0x00000000\n"
        :
        :   "r" (begin), "r" (end), "r" (syscall)
        :   "r0", "r1", "r7"
        );
}

#ifndef HEXDUMP_COLS
#define HEXDUMP_COLS 16
#endif
 
void hexdump(void *mem, unsigned int len) {
    char *szTmp = new char[10240];
    unsigned int i, j;
    
    for(i = 0; i < len + ((len % HEXDUMP_COLS) ? (HEXDUMP_COLS - len % HEXDUMP_COLS) : 0); i++) {
        /* print offset */
        if(i % HEXDUMP_COLS == 0) {
            sprintf(&szTmp[strlen(szTmp)], "0x%06x:\t", (unsigned int)mem + i);
        }

        /* print hex data */
        if(i < len) {
            sprintf(&szTmp[strlen(szTmp)], "%02x ", 0xFF & ((char*)mem)[i]);
        }
        else {/* end of block, just aligning for ASCII dump */
            sprintf(&szTmp[strlen(szTmp)], "   ");
        }
        
        /* print ASCII dump */
        if(i % HEXDUMP_COLS == (HEXDUMP_COLS - 1)) {
            for(j = i - (HEXDUMP_COLS - 1); j <= i; j++) {
                if(j >= len) /* end of block, not really printing */ {
                    sprintf(&szTmp[strlen(szTmp)], " ");
                }
                else if (((char*)mem)[j]>0x20 && ((char*)mem)[j]<0x7e) /* printable char */ {
                    sprintf(&szTmp[strlen(szTmp)], "%c", 0xFF & ((char*)mem)[j]);        
                }
                else /* other char */ {
                    sprintf(&szTmp[strlen(szTmp)], ".");
                }
            }
            sprintf(&szTmp[strlen(szTmp)], "\n");
        }
    }
    LOGD("%s", szTmp);
    delete[] szTmp;
}

void MemWriter::checkSize (size_t len) {
    if (this->len + len > this->max) {
        size_t re_size = ((this->max + len) & ~(PAGE_SIZE - 1)) + PAGE_SIZE;
        uint8_t *re_buf = new uint8_t[re_size];
        memcpy (re_buf, this->buf, this->len);
        delete[] this->buf;
        this->buf = re_buf;
        this->max = re_size;
    }
}

void MemWriter::write (uint8_t const *data, size_t len) {
    checkSize (len);
    memcpy (&this->buf[this->len], data, len);
    this->len += len;
}

bool MemWriter::sprintf (char const *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf (0, 0, fmt, ap) + 1;
    if (n < 0) {
        va_end(ap);
        return false;
    }
    checkSize (n);
    n = vsnprintf ((char*)&this->buf[this->len], n, fmt, ap);
    LOGD ("sprintf : len %d,  %s", n, (char*)&this->buf[this->len]);
    va_end(ap);
    this->len += n;
    return true;
}

/*压缩函数*/
#define CHUNK 16384
int compress_data (uint8_t const *data, size_t size, MemWriter *out) {
    int err, cur = 0, flush, result = 0;
    uint8_t *in_buf, *out_buf;
    z_stream strm;

    in_buf = new uint8_t[CHUNK];
    out_buf = new uint8_t[CHUNK];

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    err = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
    if (Z_OK != err) {
        LOGD ("deflateInit err.");
        goto compress_fail;
    }
    do {
        int have;
        strm.avail_in = (size - cur < CHUNK) ? (size - cur) : CHUNK;
        memcpy (in_buf, data + cur, strm.avail_in);
        strm.next_in = in_buf;
        flush = (size - cur < CHUNK) ? Z_FINISH : Z_NO_FLUSH;
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out_buf;
            err = deflate(&strm, flush);
            if (Z_STREAM_ERROR == err)
                goto compress_fail;
            have = CHUNK - strm.avail_out;
            out->write(out_buf, have);
        } while (strm.avail_out == 0);
        cur += CHUNK;
    } while (Z_FINISH != flush);
    deflateEnd(&strm);
    result = 1;
compress_fail:
    delete[] in_buf;
    delete[] out_buf;
    return result;
}
#undef CHUNK