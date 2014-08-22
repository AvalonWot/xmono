/*
file: xmono.cpp
author: skeu
description: hook com.tencent.Alice的数据加密点, 以获取明文数据
*/


#include <stdlib.h>
#include <deque>
#include <map>
#include <vector>
#include <assert.h>
#include <pthread.h>
#include <sys/mman.h>
#include <zlib.h>
#include <android/log.h>
#include <mono/metadata/assembly.h>
#include "mono/metadata/class.h"
#include "mono/metadata/image.h"
#include "mono/metadata/threads.h"
#include "mono/metadata/mono-debug.h"
#include "mono/metadata/debug-helpers.h"
#include "mono/metadata/profiler.h"
#include "dis-cil.h"
#include "hook.h"
#include "helper.h"
#include "ecmd.h"
#include "xmono.pb.h"

#define LOG_TAG "XMONODEBUG"
#define LOGD(fmt, args...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG, fmt, ##args)
#define LOGE(fmt, args...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG, fmt, ##args)

/*mono头文件中未声明 但实际却导出的函数和结构*/
extern "C" void g_free (void const *p);
extern "C" char *mono_pmip (void *ip);

typedef struct {
    uint32_t eip;          // pc 
    uint32_t ebp;          // fp
    uint32_t esp;          // sp
    uint32_t regs [16];
    double fregs [8];   //arm : 8
} MonoContext;
typedef int (*MonoStackFrameWalk) (MonoDomain*, MonoContext*, MonoJitInfo*, void*);
extern "C" void mono_walk_stack (MonoDomain *domain, void *jit_tls, MonoContext *start_ctx, MonoStackFrameWalk func, void *user_data);

#define CMDID(a,b,c) a = c,
enum CmdId {
    #include "const_cmdid.def"
};

struct _MonoProfiler {
    int *nul;
};
typedef struct _MonoProfiler MonoProfiler;

struct _SpecificHook {
    void *trace_func;   /*记录类函数指针*/
    void *old_func;     /*specific_hook 退出时将跳往的地址*/
    void *method;       /*MonoMethod*/
};/*specific_hook 的shellcode将不会出现在这个结构体中*/
typedef struct _SpecificHook SpecificHook;

struct _HookInfo {
    _HookInfo (MonoJitInfo *j, char *hook) {
        jinfo = j;
        specific_hook = hook;
    }
    MonoJitInfo *jinfo;
    char *specific_hook;
};
typedef struct _HookInfo HookInfo;

struct _ArmRegs {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    uint32_t r12;
    uint32_t sp;
    uint32_t lr;
    uint32_t pc;
};
typedef struct _ArmRegs ArmRegs;

/*全局变量*/
static pthread_mutex_t hooked_mutex = PTHREAD_MUTEX_INITIALIZER;
std::map<MonoMethod*, HookInfo*> hooked_method_dict;
static pthread_mutex_t replace_mutex = PTHREAD_MUTEX_INITIALIZER;
std::map<MonoMethod*, bool> replace_method_dict;
static bool trace_switch = false;

/*mono辅助函数*/
static void print_class_name (MonoClass *clazz) {
    LOGD ("class name : %s", mono_class_get_name (clazz));
}

static void print_object_class_name (MonoObject *obj) {
    MonoClass *clazz = mono_object_get_class (obj);
    print_class_name (clazz);
}

static void print_class_all_methods (MonoClass *clz) {
    void *it = 0;
    MonoMethod *m;
    while (m = mono_class_get_methods (clz, &it)) {
        char *name = mono_method_full_name (m, 1);
        LOGD ("%s", name);
        g_free (name);
    }
}

/*获取对象中的一个字段的包装函数*/
static MonoMethod *get_class_method (MonoClass *clz, char const *full_name) {
    void *it = 0;
    MonoMethod *m;
    while (m = mono_class_get_methods (clz, &it)) {
        char *name = mono_method_full_name (m, 1);
        if (strcmp (full_name, name) == 0) {
            g_free (name);
            return m;
        }
        g_free (name);
    }
}

static void get_obj_field_value (MonoObject *obj, const char *key, void *value) {
    MonoClass *clazz = mono_object_get_class (obj);
    assert (clazz);
    MonoClassField *field = mono_class_get_field_from_name (clazz, key);
    assert (field);
    void *re = 0;
    mono_field_get_value (obj, field, value);
}

static void set_obj_field_value (MonoObject *obj, char const *val_name, void *value) {
    MonoClass *clz = mono_object_get_class (obj);
    MonoClassField *field = mono_class_get_field_from_name (clz, val_name);
    mono_field_set_value (obj, field, value);
}

static char const *get_method_image_name (MonoMethod *method) {
    MonoClass *clazz = mono_method_get_class (method);
    if (!clazz) return 0;
    MonoImage *image = mono_class_get_image (clazz);
    if (!image) return 0;
    return mono_image_get_name (image);
}

static char const *get_method_class_name (MonoMethod *method) {
    MonoClass *clazz = mono_method_get_class (method);
    return mono_class_get_name (clazz);
}

static char const *get_method_namespace_name (MonoMethod *method) {
    MonoClass *clazz = mono_method_get_class (method);
    return mono_class_get_namespace (clazz);
}

static MonoMethod *get_method_with_token (std::string const &image_name, uint32_t token, std::string &err) {
    MonoImage *image = mono_image_loaded (image_name.c_str ());
    if (!image) {
        err = "dont have the image : " + image_name;
        LOGD ("%s", err.c_str ());
        return 0;
    }
    MonoMethod *method = (MonoMethod*)mono_ldtoken (image, token, 0, 0);
    if (!method) {
        err = "dont have the method, token";
        LOGD ("%s", err.c_str ());
        return 0;
    }
    return method;
}
/*mono辅助函数END*/

#define SP_BLOCK_SIZE 108
static std::deque<char*> mem_cache;
static pthread_mutex_t alloc_mutex = PTHREAD_MUTEX_INITIALIZER;
static char *alloc_specific_trampo () {
    pthread_mutex_lock (&alloc_mutex);
    if (mem_cache.empty()) {
        LOGD ("alloc specific trampo cache...");
        size_t map_size = 10240 * SP_BLOCK_SIZE;
        char *p = (char*)mmap (0, map_size, PROT_EXEC | PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        for (int i = 0; i < map_size / SP_BLOCK_SIZE; i++) {
            mem_cache.push_back (p + i * SP_BLOCK_SIZE);
        }
        LOGD ("alloc specific trampo cache end...");
    }
    char *r = mem_cache.front ();
    mem_cache.pop_front ();
    pthread_mutex_unlock (&alloc_mutex);
    return r;
}

static char *specific_hook (void *org, void *method_obj, void *func) {
    /*  ______________
     * |__common_func_ 0
     * |___old_func___ 4
     * |__monomethod__ 8
     * |_____code[]___ 12
     */
    char *p = alloc_specific_trampo ();
    /*Fixme : 每次更新这段跳板代码都是蛋疼, 有没有方便高效稳定的方法?*/
    unsigned char code[96] = {
        0x0F, 0x00, 0x2D, 0xE9, 0xFF, 0xFF, 0x2D, 0xE9, 0x50, 0x00, 0x4D, 0xE2, 0x34, 0x00, 0x8D, 0xE5, 
        0x3C, 0xE0, 0x8D, 0xE5, 0x40, 0x10, 0x4D, 0xE2, 0x30, 0x40, 0xA0, 0xE3, 0x04, 0x40, 0x4F, 0xE0, 
        0x08, 0x00, 0x94, 0xE5, 0x0D, 0x20, 0xA0, 0xE1, 0x0F, 0xE0, 0xA0, 0xE1, 0x00, 0xF0, 0x94, 0xE5, 
        0xFF, 0x1F, 0xBD, 0xE8, 0x04, 0xE0, 0x9D, 0xE5, 0x1C, 0xD0, 0x8D, 0xE2, 0x01, 0x80, 0x2D, 0xE9, 
        0x58, 0x00, 0xA0, 0xE3, 0x00, 0x00, 0x4F, 0xE0, 0x04, 0x00, 0x90, 0xE5, 0x04, 0x00, 0x8D, 0xE5, 
        0x01, 0x80, 0xBD, 0xE8, 0x00, 0xF0, 0x20, 0xE3, 0x00, 0xF0, 0x20, 0xE3, 0x00, 0xF0, 0x20, 0xE3
    };
    /*
     * stmfd sp!, {r0-r3} 
     * stmfd sp!, {r0-r12, sp, lr, pc}
     * sub r0, sp, #80      //函数调用前的sp                 |
     * str r0, [sp, #52]    //设置保存的reg.sp为原来的sp     |-> 都是为了堆栈回溯做准备
     * str lr, [sp, #60]    //设置保存的reg.pc为lr           |
     * sub r1, sp, #64      //2.参数数组
     * ldr r4, =48
     * sub r4, pc, r4
     * ldr r0, [r4, #8]     //1.method ptr
     * mov r2, sp           //3.保存的reg环境块, 用于堆栈回溯
     * mov lr, pc
     * ldr pc, [r4]
     * ldmfd sp!, {r0-r12}
     * ldr lr, [sp, #4]     //恢复lr, 但不恢复sp, pc
     * add sp, sp, #28      //清除掉栈中的 {sp, lr, pc} 以及 {r0-r3}
     * stmfd sp!, {r0, pc}
     * ldr r0, =88
     * sub r0, pc, r0
     * ldr r0, [r0, #4]
     * str r0, [sp, #4]     //设置old_func域中的指针为继续执行的PC
     * ldmfd sp!, {r0, pc}
     */
    memcpy (p + 12, code, sizeof (code));
    memcpy (p, &func, 4);
    memcpy (p + 8, &method_obj, 4);

    if (arm_hook (org, p + 12, (void**)(p + 4)) == 0) {
        return 0;
    }
    cache_flush ((uint32_t)p, (uint32_t)(p + SP_BLOCK_SIZE));
    return p;
}

static int walk_stack (MonoDomain *domain, MonoContext *ctx, MonoJitInfo *jit_info, void *user_data) {
    MemWriter *writer = (MemWriter*)user_data;
    MonoMethod *method =mono_jit_info_get_method (jit_info);
    if (!method) {
        writer->sprintf("%s", "jit_info no method!");
        return 0;
    }
    char const *m = get_method_image_name (method);
    m = m ? m : "NO_IMAGE_NAME";
    char const *n = mono_method_full_name (method, 1);
    if (n) {
        writer->sprintf ("[%s] %s [%08X]\n", m, n, mono_method_get_token (method));
        g_free (n);
    } else {
        writer->sprintf ("[%s] %s [%08X]\n", m, "NO_METHOD_NAME", mono_method_get_token (method));
    }
    return 0; /*一直walk, 直到栈帧尽头*/
}

void stack_trace (MonoMethod *method, void *args[], ArmRegs *regs) {
    LOGD ("stack_trace be call!");
    MonoContext ctx;
    ctx.eip = regs->pc;
    ctx.esp = regs->sp;
    ctx.ebp = regs->r11;
    memcpy (ctx.regs, regs, 4 * 16);
    MemWriter writer;
    /*因为当前被跟踪的函数还没被调用(当前在specific_trampo中), 所以这里手动写入当前函数的跟踪结构*/
    char const *m = get_method_image_name (method);
    m = m ? m : "NO_IMAGE_NAME";
    char const *n = mono_method_full_name (method, 1);
    if (n) {
        writer.sprintf ("[%s]%s[%08X]\n", m, n, mono_method_get_token (method));
        g_free (n);
    } else {
        writer.sprintf ("[%s]%s[%08X]\n", m, "NO_METHOD_NAME", mono_method_get_token (method));
    }
    mono_walk_stack (mono_domain_get (), 0, &ctx, walk_stack, &writer);

    xmono::StackTraceRsp rsp;
    rsp.set_err (true);
    rsp.set_stack (std::string ((char*)writer.getBuffPtr (), writer.getBuffSize ()));
    std::string out;
    rsp.SerializeToString (&out);
    ecmd_send (XMONO_ID_STACK_TRACE_RSP, (uint8_t const*)out.c_str (), out.size ());
}

/*反汇编method的IL*/
static char const *disil_method (MonoMethod *method) {
    MonoMethodHeader *header = mono_method_get_header (method);
    uint32_t len = 0;
    uint8_t const *code = mono_method_header_get_code (header, &len, 0);
    return mono_disasm_code (0, method, code, code + len);
}

static void send_trace_log (std::map<MonoMethod*, int> const &dict) {
    MemWriter sbuf(0x100000);
    MemWriter writer(0x100000);
    std::map<MonoMethod*, int>::const_iterator p;
    for (p = dict.begin (); p != dict.end (); p++) {
        MonoMethod *method = p->first;
        int count = p->second;
        char const *m = get_method_image_name (method);
        char *n = mono_method_full_name (method, 0);
        char str[256];
        sbuf.sprintf ("[%s] %s [%08X]|%d\n", m, n, mono_method_get_token (method), count);
        g_free (n);
    }
    if (!compress_data (sbuf.getBuffPtr (), sbuf.getBuffSize (), &writer)) {
        LOGD ("compress_data err!");
        return;
    }
    /*Fixme : 这里写在使用protocolbuf之前, 因此非常不河蟹的出现了不使用pb的封包*/
    ecmd_send (XMONO_ID_TRACE_REPORT, writer.getBuffPtr (), writer.getBuffSize ());
    return;
}

/**
 * 该函数由specific_hook的shellcode发起调用, 原本有三个参数, 这里利用arm是寄存器传参的特性, 省略了两个,
 * 因此移植时需要注意
 */
pthread_mutex_t trace_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static void func_trace (MonoMethod *method) {
    pthread_mutex_lock (&trace_list_mutex);
    static std::map<MonoMethod*, int>trace_log;
    if (trace_switch == false) {
        if (!trace_log.empty ()) {
            send_trace_log (trace_log);
            trace_log.clear ();
        }
        pthread_mutex_unlock (&trace_list_mutex);
        return; /*Fixme : 这个地方再考虑, 重复代码*/
    }
    if (trace_log.find (method) != trace_log.end ())
        trace_log[method] += 1;
    else
        trace_log[method] = 1;
    pthread_mutex_unlock (&trace_list_mutex);
    return;
}

/*监控需要hook的函数*/
static void profile_jit_result (MonoProfiler *prof, MonoMethod *method, MonoJitInfo* jinfo, int result) {
    if (result == MONO_PROFILE_FAILED) return;
    if (mono_method_get_token (method) == 0) return;                /*一般是动态生成的marshall*/
    uint32_t iflags;
    int flag = mono_method_get_flags (method, &iflags);
    if (iflags != 0) return;                                        /*iflags非0 一般是一些native和特殊的method实现*/
    if (mono_jit_info_get_code_size (jinfo) < 4) return;            /*代码段太小, 无法hook*/

    void *p = mono_jit_info_get_code_start (jinfo);
    if (p == 0) {
        LOGD ("function code size is null");
        return;
    }

        /*测试jit函数是否是mov r12, sp*/
    if (*(uint32_t*)p != 0xE1A0C00D)
         LOGD ("exception func : %s , %p", mono_method_get_name (method), p);

    /*TODO : 增加可配置的image和函数列表*/
    if (strcmp (get_method_image_name (method), "Assembly-CSharp") != 0) return;
    if (strcmp (mono_method_get_name (method), ".ctor") == 0) return;
    if (strcmp (mono_method_get_name (method), ".cctor") == 0) return;
    if (strcmp (mono_method_get_name (method), "set") == 0) return;
    if (strcmp (mono_method_get_name (method), "get") == 0) return;
    if (strcmp (mono_method_get_name (method), "Update") == 0) return;
    if (strcmp (mono_method_get_name (method), "LateUpdate") == 0) return;
    if (strcmp (mono_method_get_name (method), "OnGUI") == 0) return;

    /*TODO : 需要一个容器来存储还未编译, 但又想hook的函数*/

    bool donthook = false;
    pthread_mutex_lock (&replace_mutex);
    if (replace_method_dict.find (method) != replace_method_dict.end ())
        donthook = true;
    pthread_mutex_unlock (&replace_mutex);
    if (donthook) return;

    char *hook = specific_hook (p, method, (void*)func_trace);
    if (hook == 0) {
        /*将失败的hook也放到一个表里面*/
        LOGD ("hook err : %s", mono_method_get_name (method));
        return;
    }
    pthread_mutex_lock (&hooked_mutex);
    hooked_method_dict[method] = new HookInfo(jinfo, hook);
    pthread_mutex_unlock (&hooked_mutex);
    LOGD ("hook func : %s , %p", mono_method_get_name (method), p);
    return;
}

/*mono正常退出时 该函数被调用*/
static void mono_shutdown (MonoProfiler *prof) {
    LOGD ("mono over.");
}

static void install_jit_profile () {
    MonoProfiler *prof = new MonoProfiler;
    mono_profiler_install (prof, mono_shutdown);
    mono_profiler_install_jit_end (profile_jit_result);
    mono_profiler_set_events (MONO_PROFILE_JIT_COMPILATION);
}

static void foreach_domain_callback (MonoDomain *domain, void *user_data) {
    std::vector<int> *v = (std::vector<int>*)user_data;
    v->push_back (mono_domain_get_id (domain));
}

static void list_assemblys (Package *pkg) {
    xmono::ListDomainRsp rsp;
    std::vector<int> v;
    mono_domain_foreach (foreach_domain_callback, &v);
    for (int i = 0; i < v.size (); i++) {
        rsp.add_id (v[i]);
    }
    rsp.set_err (true);
    std::string out;
    rsp.SerializeToString (&out);
    ecmd_send (XMONO_ID_LIST_DOMAIN_RSP, (uint8_t const*)out.c_str (), out.size ());
    return;
}

static void unset_stack_trace (Package *pkg) {
    xmono::UnStackTraceReq req;
    std::string str((char*)pkg->body, pkg->all_len - sizeof (Package));
    if (!req.ParseFromString (str)) {
        LOGD ("xmono::StackTraceReq ParseFromString err!");
        return;
    }
    std::string err;
    MonoMethod *method = get_method_with_token(req.image_name ().c_str (), req.method_token (), err);
    if (!method) {
        return;
    }
    pthread_mutex_lock (&hooked_mutex);
    if (hooked_method_dict.find (method) != hooked_method_dict.end ()) {
        HookInfo *info = hooked_method_dict[method];
        SpecificHook *shook = (SpecificHook*)info->specific_hook;
        shook->trace_func = (void*)func_trace;
    }
    pthread_mutex_unlock (&hooked_mutex);
}

static void set_stack_trace (Package *pkg) {
    /*Fixme : 需要产生一个shellcode, 来控制记录多少次, 栈深多少等问题*/
    xmono::StackTraceReq req;
    xmono::StackTraceRsp rsp;
    std::string str((char*)pkg->body, pkg->all_len - sizeof (Package));
    if (!req.ParseFromString (str)) {
        LOGD ("xmono::StackTraceReq ParseFromString err!");
        return;
    }
    do {
        std::string err;
        MonoMethod *method = get_method_with_token(req.image_name ().c_str (), req.method_token (), err);
        if (!method) {
            rsp.set_err (false);
            rsp.set_err_str (err);
            break;
        }
        pthread_mutex_lock (&hooked_mutex);
        if (hooked_method_dict.find (method) != hooked_method_dict.end ()) {
            HookInfo *info = hooked_method_dict[method];
            SpecificHook *shook = (SpecificHook*)info->specific_hook;
            shook->trace_func = (void*)stack_trace;
            rsp.set_err (true);
        } else {
            /*Fixme : 这里不应该返回错误, 而是加入一个队列当函数被编译时 hook后直接替换*/
            rsp.set_err (false);
            rsp.set_err_str ("can not find method's hook info.");
        }
        pthread_mutex_unlock (&hooked_mutex);
    }while (0);
    if (rsp.err ()) return;   /*正常设置stack_trace的情况下 不用通知客户端*/
    std::string out;
    rsp.SerializeToString (&out);
    ecmd_send (XMONO_ID_STACK_TRACE_RSP, (uint8_t const*)out.c_str (), out.size ());
}

static void replace_method (Package *pkg) {
    xmono::ReplaceMethodReq req;
    xmono::ReplaceMethodRsp rsp;
    std::string str((char*)pkg->body, pkg->all_len - sizeof (Package));
    if (!req.ParseFromString (str)) {
        LOGD ("xmono::ReplaceMethodReq ParseFromString err!");
        return;
    }
    std::string err;
    void *p, *old_p;
    uint8_t *code;
    int code_size;
    MonoMethodHeader *mh;
    MonoThread *thread;
    MonoMethod *new_method;
    MonoMethod * method = get_method_with_token (req.image_name ().c_str (), req.method_token (), err);
    MonoDomain *domain = mono_domain_get_by_id (req.domain_id ());
    if (!domain) {
        rsp.set_err (false);
        rsp.set_msg ("can not get the domain from id");
        goto replace_method_end;
    }
    if (!method) {
        rsp.set_err (false);
        rsp.set_msg (err);
        goto replace_method_end;
    }
    mh = mono_method_get_header (method);
    if (req.ex_size () != mono_method_header_get_num_clauses (mh)) {
        rsp.set_err (false);
        rsp.set_msg ("ex size != mono_method_header_clauses size!");
        goto replace_method_end;
    }
    for (int i = 0; i < req.ex_size (); i++) {
        xmono::ReplaceMethodReq_ExceptionClause const &e = req.ex (i);
        void *iter = 0;
        MonoExceptionClause *clauses = &mh->clauses[i];
        MonoExceptionClause *old_e = (MonoExceptionClause*)iter;
        old_e->try_offset = e.try_offset ();
        old_e->try_len = e.try_len ();
        old_e->handler_offset = e.handler_offset ();
        old_e->handler_len = e.handler_len ();
    }
    code = new uint8_t[req.new_code ().size ()];
    memcpy (code, req.new_code ().c_str (), req.new_code ().size ());
    mh->code = code;
    mh->code_size = req.new_code ().size ();
    thread = mono_thread_attach (domain);
    /*128 是一个估计值, 在未来可能不稳定, 但当前只能如此*/
    new_method = (MonoMethod*)calloc (128, 1); /*这个地方用malloc优于用new*/
    memcpy (new_method, method, 128);

    pthread_mutex_lock (&replace_mutex);
    replace_method_dict[new_method] = true;
    pthread_mutex_unlock (&replace_mutex);

    p = mono_compile_method (new_method);
    memcpy (hooked_method_dict[method]->specific_hook + 4, &p, 4);

    pthread_mutex_lock (&hooked_mutex);
    old_p = mono_jit_info_get_code_start (hooked_method_dict[method]->jinfo);
    pthread_mutex_unlock (&hooked_mutex);

    mono_thread_detach (thread);
    LOGD ("compile method, new ptr : %p, old ptr : %p", p, old_p);
    rsp.set_err (true);
    rsp.set_msg ("replace_method successful.");
replace_method_end:
    std::string out;
    rsp.SerializeToString (&out);
    ecmd_send (XMONO_ID_REPLACE_METHOD_RSP, (uint8_t const*)out.c_str (), out.size ());
    return;
}

static void disasm_method (Package *pkg) {
    xmono::DisasmMethodReq req;
    xmono::DisasmMethodRsp rsp;
    std::string str((char*)pkg->body, pkg->all_len - sizeof (Package)); //Fixme : 修复头部all_len是总长的问题
    if (!req.ParseFromString (str)) {
        LOGD ("xmono::DisasmMethodReq ParseFromString err!");
        return;
    }
    std::string err;
    MonoMethod *method = get_method_with_token (req.image_name ().c_str (), req.method_token (), err);
    MonoImage *image = mono_image_loaded (req.image_name ().c_str ());
    if (image && method) {
        MemWriter writer(4096);
        char const *mname = mono_method_full_name (method, 1);
        if (mname) {
            writer.sprintf ("//[%s]%s[%08X]\n", get_method_image_name (method), mname, mono_method_get_token (method));
            g_free (mname);
        }
        MonoMethodHeader *header = mono_method_get_header (method);
        disassemble_cil (image, header, &writer); /*Fixme : disassemble_cil需要在失败时返回更多信息*/
        rsp.set_err (true);
        LOGD ("writer : %s", writer.getBuffPtr ());
        std::string s((char*)writer.getBuffPtr (), writer.getBuffSize ());
        rsp.set_disasm_code (s);
        //这部分只是测试用的
        uint32_t asm_size = 0;
        uint8_t const *p = mono_method_header_get_code (header, &asm_size, 0);
        std::string asm_code ((char const*)p, asm_size);
        rsp.set_asm_code (asm_code);
    } else {
        rsp.set_err (false);
        rsp.set_err_str (err);
    }
    std::string out;
    rsp.SerializeToString (&out);
    ecmd_send (XMONO_ID_DISASM_METHOD_RSP, (uint8_t const*)out.c_str (), out.size ());
    return;
}

static void start_func_trace (Package *pkg) {
    trace_switch = true;
}

static void stop_func_trace (Package *pkg) {
    trace_switch = false;
}

static void ecmd_err_callback () {
    LOGD ("ecmd err!");
    exit (1);
}

static void init_network () {
    if (!ecmd_init (ecmd_err_callback)) {
        LOGD ("ecmd_init err : %s", ecmd_err_str ());
        exit (1);
    }
    if (!ecmd_start_server ("127.0.0.1", 21187)) {
        LOGD ("ecmd_start_server err : %s", ecmd_err_str ());
        exit (1);
    }
    ecmd_register_resp (XMONO_ID_FUNC_TRACE_START, start_func_trace);
    ecmd_register_resp (XMONO_ID_FUNC_TRACE_STOP, stop_func_trace);
    ecmd_register_resp (XMONO_ID_DISASM_METHOD_REP, disasm_method);
    ecmd_register_resp (XMONO_ID_REPLACE_METHOD_REP, replace_method);
    ecmd_register_resp (XMONO_ID_LIST_DOMAIN_REP, list_assemblys);
    ecmd_register_resp (XMONO_ID_STACK_TRACE_REP, set_stack_trace);
    ecmd_register_resp (XMONO_ID_UNSTACK_TRACE_REP, unset_stack_trace);
    LOGD ("ecmd init over.");
}

extern "C" int so_main() {
    LOGD ("hello, xmono");
    install_jit_profile ();
    init_network ();
    return 0;
}