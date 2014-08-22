/*
file: dis-cil.cpp
author: skeu
description: 从mono项目中的dis子项目copy而来, 做了部分修改
*/

#include <stdint.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <ctype.h>
#include <android/log.h>
#include "mono/metadata/image.h"
#include "mono/metadata/appdomain.h"
#include "mono/metadata/metadata.h"
#include "mono/metadata/opcodes.h"
#include "mono/metadata/class.h"
#include "mono/metadata/object.h"
#include "mono/metadata/debug-helpers.h"
#include "helper.h"

extern "C" void g_free (void const*);

#define LOG_TAG "XMONODEBUG"
#define LOGD(fmt, args...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG, fmt, ##args)
#define LOGE(fmt, args...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG, fmt, ##args)

#define CODE_INDENT assert (indent_level < 512); \
    indent[indent_level*2] = ' ';   \
    indent[indent_level*2+1] = ' '; \
    ++indent_level; \
    indent[indent_level*2] = 0;
#define CODE_UNINDENT assert (indent_level);  \
    --indent_level; \
    indent[indent_level*2] = 0;


static int16_t read16 (uint8_t const *p) {
    int16_t n;
    memcpy (&n, p, 2);
    return n;
}

static int read32 (uint8_t const *p) {
    int n;
    memcpy (&n, p, 4);
    return n;
}

static int64_t read64 (uint8_t const *p) {
    int64_t n;
    memcpy (&n, p, 8);
    return n;
}

static void readr4 (uint8_t const *p, float *r) {
    memcpy (r, p, 4);
}

static void readr8 (uint8_t const *p, double *r) {
    memcpy (r, p, 8);
}

static char const *chrescape (const char chr) {
    char const *res = 0;
    switch (chr) {
    case '\r': res = "\\r"; break;
    case '\a': res = "\\a"; break;
    case '\b': res = "\\b"; break;
    case '\t': res = "\\t"; break;
    case '\n': res = "\\n"; break;
    case '\v': res = "\\v"; break;
    case '\f': res = "\\f"; break;
    }
    return res;
}

static void get_encoded_user_string (uint8_t const *ptr, int len, MemWriter *writer) {
    writer->sprintf ("//%s", "\"");
    for (int i = 0; i + 1 < len; i += 2) {
        if (ptr[i + 1]) {
            writer->sprintf ("\\x%02X\\x%02X", ptr[i], ptr[i + 1]);
            continue;
        }
        if (isprint (ptr[i])) {
            writer->write (&ptr[i], 1);
            continue;
        }
        char const *e = chrescape (ptr[i]);
        if (e) {
            writer->sprintf ("%s", e);
            continue;
        }
        writer->sprintf ("\\x%02X", ptr[i]);
    }
    writer->sprintf ("%s", "\"");
    return;
}


bool disassemble_cil (MonoImage *m, MonoMethodHeader *mh, MemWriter *writer) {
    uint32_t size = 0;
    unsigned const char *start = mono_method_header_get_code (mh, &size, 0);
    unsigned const char *end = start + size;
    unsigned const char *ptr = start;
    MonoOpcode const *entry;
    char indent[1024];
    int indent_level = 0;
    bool in_fault = 0;
    char const *clause_names[] = {".catch", ".filter", ".finally", "", ".fault"};
    bool *trys = 0;
    indent [0] = 0;

    int num_clauses = mono_method_header_get_num_clauses (mh);
    void *iter = 0;
    MonoExceptionClause *clauses = new MonoExceptionClause[num_clauses];
    for (int i = 0; i < num_clauses; i++) {
        mono_method_header_get_clauses (mh, 0, &iter, &clauses[i]);

        writer->sprintf ("// out clause %d: from %d len=%d, handler at %d, %d \n",
            clauses[i].flags, clauses[i].try_offset, clauses[i].try_len, clauses[i].handler_offset, clauses[i].handler_len);
    }

    if (num_clauses) {
        trys = new bool[num_clauses];
        trys [0] = 1;
        iter = 0;
        for (int i = 0; i < num_clauses; i++) {
#define jcl clauses [j] 
#define cl clauses [i]  
            trys [i] = 1;
            for (int j = 0; j < i; j++) {
                if (cl.try_offset == jcl.try_offset && cl.try_len == jcl.try_len) {
                    trys [i] = 0;
                    break;
                }
            }
#undef jcl
#undef cl
        }
    }

    LOGD ("run in loop : ptr %p, end %p", ptr, end);
    while (ptr < end){
        for (int i = num_clauses - 1; i >= 0 ; --i) {
            if (ptr == start + clauses[i].try_offset && trys [i]) {
                writer->sprintf ("%s.try {  %d\n", indent, i);
                CODE_INDENT;
            }

            if (ptr == start + clauses[i].handler_offset) {
                if (clauses[i].flags == MONO_EXCEPTION_CLAUSE_FILTER) {
                    CODE_UNINDENT;
                    writer->sprintf ("%s} {  %d\n", indent, i);
                } else {
                    if (clauses[i].flags) {
                        writer->sprintf ("%s%s {  %d\n", indent, clause_names [clauses[i].flags], i);
                    } else {
                        MonoClass *c = clauses[i].data.catch_class;
                        if (!c)
                            writer->sprintf ("[x]type : %s, catch_class is null!\n", clause_names [clauses[i].flags]);
                        else {
                            MonoImage *image = mono_class_get_image (c);
                            uint32_t token = mono_class_get_type_token (c);
                            char const *name = mono_class_name_from_token (image, token);
                            writer->sprintf ("%s%s {  %d //[%08X]%s\n", indent, clause_names [clauses[i].flags], i, token, name);
                            g_free (name);
                        }
                    }
                }
                CODE_INDENT;
                if (clauses[i].flags == MONO_EXCEPTION_CLAUSE_FAULT)
                    in_fault = 1;
            }
            if (clauses[i].flags == MONO_EXCEPTION_CLAUSE_FILTER && ptr == start + clauses[i].data.filter_offset) {
                   writer->sprintf ("%s%s { %d\n", indent, clause_names[1], i);
                   CODE_INDENT;
            }
        }
        writer->sprintf ("%sIL_%04x:\t", indent, (int) (ptr - start));
        int mcodei = *ptr;
        if (*ptr == 0xfe){
            ptr++;
            mcodei = *ptr + 256;
        } 
        entry = &mono_opcodes [mcodei];

        if (in_fault && entry->opval == 0xDC)
            writer->sprintf (" %s", "endfault");
        else
            writer->sprintf (" %s ", mono_opcode_name (mcodei));
        ptr++;
        switch (entry->argument){

        case MonoInlineBrTarget: {
            int target = read32 (ptr);
            writer->sprintf ("IL_%04x\n", ((int) (ptr - start)) + 4 + target);
            ptr += 4;
            break;
        }
            
        case MonoInlineField: {
            char const *s = 0;
            uint32_t token = read32 (ptr);
            MonoClassField *field = (MonoClassField*)mono_ldtoken (m, token, 0, 0);
            if (field)
                s = mono_field_full_name (field);
            else
                s = "无法解析field的名字";
            writer->sprintf ("[%08X]    //%s", token, s);
            if (field)
                g_free (s);
            ptr += 4;
            break;
        }
        
        case MonoInlineI: {
            int value = read32 (ptr);

            writer->sprintf ("0x%x", value);
            ptr += 4;
            break;
        }
        
        case MonoInlineI8: {
            int64_t top = read64 (ptr);

            writer->sprintf ("0x%llx", (long long) top);
            ptr += 8;
            break;
        }
        
        case MonoInlineMethod: {
            uint32_t token = read32 (ptr);
            MonoMethod *method = (MonoMethod*)mono_ldtoken (m, token, 0, 0);
            if (!method) {
                writer->sprintf("//无法解析token : %08X\n", token);
                return false;
            }
            char const *sig = mono_method_full_name (method, true);
            writer->sprintf ("[%08X] //%s", token, sig);
            g_free (sig);
            ptr += 4;
            break;
        }
        
        case MonoInlineNone:
            break;
            
        case MonoInlineR: {
            double r;
            int inf;
            readr8 (ptr, &r);
            inf = isinf (r);
            if (inf == -1) 
                writer->sprintf ("(00 00 00 00 00 00 f0 ff) //负无限大, 暂时无法被编译"); /* negative infinity */
            else if (inf == 1)
                writer->sprintf ("(00 00 00 00 00 00 f0 7f) //正无限大, 暂时无法被编译"); /* positive infinity */
            else if (isnan (r))
                writer->sprintf ("(00 00 00 00 00 00 f8 ff) //无效浮点数, 无法被编译"); /* NaN */
            else {
                /*Fixme : 处理浮点数的输出*/
                writer->sprintf ("%lf", r);
            }
            ptr += 8;
            break;
        }
        
        case MonoInlineSig: {
            uint32_t token = read32 (ptr);
            writer->sprintf ("[%08X]    /*signature*/", token);
            ptr += 4;
            break;
        }
        
        case MonoInlineString: {
            /*Fixme : 支持非英文的转换, 需要一个ICU库*/
            uint32_t token = read32 (ptr);
            char const *us_ptr = mono_metadata_user_string (m, token & 0xffffff);
            int len = mono_metadata_decode_blob_size (us_ptr, (const char**)&us_ptr);
            writer->sprintf ("[%08X] ", token);
            get_encoded_user_string ((uint8_t const*)us_ptr, len, writer);
            /*
             * See section 23.1.4 on the encoding of the #US heap   //奇葩注释...这里是说的ECMA  Common Language Infrastructure 文档
                                                                       当前章节已经变为II.24.2.4
             */
            ptr += 4;
            break;
        }

        case MonoInlineSwitch: {
            uint32_t count = read32 (ptr);
            const unsigned char *endswitch;
            uint32_t n;
            
            ptr += 4;
            endswitch = ptr + sizeof (uint32_t) * count;
            writer->sprintf (count > 0 ? "( " : "( )");
            CODE_INDENT;
            for (n = 0; n < count; n++){
                writer->sprintf ("IL_%04x%s",
                         (int)(endswitch - start + read32 (ptr)), n == count - 1 ? ")" : ",");
                ptr += 4;
            }
            CODE_UNINDENT;
            break;
        }

        case MonoInlineTok: {
            /*Fixme : 完善token显示*/
            uint32_t token = read32 (ptr);
            writer->sprintf ("[%08X] //token", token);
            ptr += 4;
            break;
        }
        
        case MonoInlineType: {
            /*Fixme : 完善Type显示*/
            uint32_t token = read32 (ptr);
            writer->sprintf ("[%08X] //type", token);
            ptr += 4;
            break;
        }

        case MonoInlineVar: {
            uint16_t var_idx = read16 (ptr);
            writer->sprintf ("0x%04x", var_idx);
            ptr += 2;
            break;
        }

        case MonoShortInlineBrTarget: {
            signed char x = *ptr;
            writer->sprintf ("IL_%04x\n", (int)(ptr - start + 1 + x));
            ptr++;
            break;
        }

        case MonoShortInlineI: {
            char x = *ptr;
            if (x > 0x1F && x < 0x7F)
                writer->sprintf ("0x%02X    //\'%c\'", x, x);
            else if (chrescape (x))
                writer->sprintf ("0x%02X    //\'%s\'", x, chrescape (x));
            else
                writer->sprintf ("0x%02X", x);
            ptr++;
            break;
        }

        case MonoShortInlineR: {
            float f;
            int inf;
            
            readr4 (ptr, &f);

            inf = isinf (f);
            if (inf == -1) 
                writer->sprintf ("(00 00 80 ff) //负无限大, 暂时无法被编译"); /* negative infinity */
            else if (inf == 1)
                writer->sprintf ("(00 00 80 7f) //正无限大, 暂时无法被编译"); /* positive infinity */
            else if (isnan (f))
                writer->sprintf ("(00 00 c0 ff) //无效浮点数, 无法被编译"); /* NaN */
            else {
                writer->sprintf ("%f", f);
            }
            ptr += 4;
            break;
        }

        case MonoShortInlineVar: {
            unsigned char x = *ptr;
            writer->sprintf ("%d", (int) x);
            ptr++;
            break;
        }
        default:
            break;
        }
        writer->sprintf ("\n");
        for (int i = 0; i < num_clauses; ++i) {
            if (ptr == start + clauses[i].try_offset + clauses[i].try_len && trys [i]) {
                CODE_UNINDENT;
                writer->sprintf ("%s} // end .try %d\n", indent, i);
            }
            if (ptr == start + clauses[i].handler_offset + clauses[i].handler_len) {
                CODE_UNINDENT;
                writer->sprintf ("%s} // end handler %d\n", indent, i);
                if (clauses[i].flags == MONO_EXCEPTION_CLAUSE_FAULT)
                    in_fault = 0;
            }
        }
    }
    if (trys)
        delete[] trys;
    if (clauses)
        delete[] clauses;
    LOGD ("return from dis-cil");
}