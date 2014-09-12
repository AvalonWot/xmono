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
#include "mono/metadata/assembly.h"
#include "mono/metadata/class.h"
#include "mono/metadata/image.h"
#include "mono/metadata/threads.h"
#include "mono/metadata/mono-debug.h"
#include "mono/metadata/debug-helpers.h"
#include "mono/metadata/profiler.h"
#include "mono/metadata/attrdefs.h"
#include "lua/lua.hpp"
#include "dis-cil.h"
#include "hook.h"
#include "helper.h"
#include "mono-helper.h"
#include "lua-mono.h"
#include "ecmd.h"
#include "xmono.pb.h"

#define LOG_TAG "XMONODEBUG"
#define LOGD(fmt, args...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG, fmt, ##args)
#define LOGE(fmt, args...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG, fmt, ##args)

#define CMDID(a,b,c) a = c,
enum CmdId {
    #include "const_cmdid.def"
};

/*前置自定义类声明*/
struct HookFuncs;
struct ArmRegs;

/*前置函数声明*/
static lua_State *lua_env_new ();
static void common_hook (HookFuncs *hooks, MonoMethod *method, void *args[], ArmRegs *regs);

typedef void (*CommonHookFunc) (HookFuncs*, MonoMethod*, void*[], ArmRegs*);
typedef void (*SpecHookFunc) (MonoMethod*, void**, ArmRegs*);

struct ArmRegs {
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

/*HookFuncs.mark 的掩码定义*/
#define FUNC_TRACE_MARK 1u
#define STACK_TRACE_MARK 2u
#define LUA_HOOK_MARK 4u

/*包含所有类型hook的结构体*/
struct HookFuncs {
    uint32_t mark;
    SpecHookFunc func_trace;
    SpecHookFunc stack_trace;
    SpecHookFunc lua_hook;
};

/*通用hook的头部*/
struct SpecificHook {
    CommonHookFunc common_hook;     /*通用hook函数指针*/
    HookFuncs *hooks;               /*hook函数列表*/
    void *old_func;                 /*specific_hook 退出时将跳往的地址*/
    void *method;                   /*MonoMethod*/
};/*specific_hook 的shellcode将不会出现在这个结构体中*/

struct HookInfo {
    HookInfo (MonoJitInfo *j, SpecificHook *hook) {
        jinfo = j;
        specific_hook = hook;
    }
    MonoJitInfo *jinfo;
    SpecificHook *specific_hook;
};

/*全局变量*/
static pthread_mutex_t hooked_mutex = PTHREAD_MUTEX_INITIALIZER;
std::map<MonoMethod*, HookInfo*> hooked_method_dict;
static pthread_mutex_t addon_hook_mutex = PTHREAD_MUTEX_INITIALIZER;
std::map<MonoMethod*, bool> addon_hook_dict;
static pthread_mutex_t replace_mutex = PTHREAD_MUTEX_INITIALIZER;
std::map<MonoMethod*, bool> replace_method_dict;
static pthread_mutex_t lua_hook_mutex = PTHREAD_MUTEX_INITIALIZER;
std::map<MonoMethod*, char const*> lua_hook_dict;
static bool trace_switch = false;

/*Fixme : 每次更新这段跳板代码都是蛋疼, 有没有方便高效稳定的方法?*/
unsigned char shellcodes[92] = {
    0x0F, 0x00, 0x2D, 0xE9, 0xFF, 0xFF, 0x2D, 0xE9, 0x50, 0x00, 0x4D, 0xE2, 0x34, 0x00, 0x8D, 0xE5, 
    0x3C, 0xE0, 0x8D, 0xE5, 0x40, 0x20, 0x8D, 0xE2, 0x34, 0x40, 0xA0, 0xE3, 0x04, 0x40, 0x4F, 0xE0, 
    0x04, 0x00, 0x94, 0xE5, 0x0C, 0x10, 0x94, 0xE5, 0x0D, 0x30, 0xA0, 0xE1, 0x0F, 0xE0, 0xA0, 0xE1, 
    0x00, 0xF0, 0x94, 0xE5, 0xFF, 0x1F, 0xBD, 0xE8, 0x04, 0xE0, 0x9D, 0xE5, 0x0C, 0xD0, 0x8D, 0xE2, 
    0x0F, 0x00, 0xBD, 0xE8, 0x01, 0x80, 0x2D, 0xE9, 0x64, 0x00, 0xA0, 0xE3, 0x00, 0x00, 0x4F, 0xE0, 
    0x08, 0x00, 0x90, 0xE5, 0x04, 0x00, 0x8D, 0xE5, 0x01, 0x80, 0xBD, 0xE8, 
} ;
/*
 * stmfd sp!, {r0-r3} 
 * stmfd sp!, {r0-r12, sp, lr, pc}
 * sub r0, sp, #80      //函数调用前的sp                 |
 * str r0, [sp, #52]    //设置保存的reg.sp为原来的sp     |-> 都是为了堆栈回溯做准备
 * str lr, [sp, #60]    //设置保存的reg.pc为lr           |
 * add r2, sp, #64      //3.参数数组
 * ldr r4, =52
 * sub r4, pc, r4
 * ldr r0, [r4, #4]     //1.hooks
 * ldr r1, [r4, #0x0C]  //2.method ptr
 * mov r3, sp           //4.保存的reg环境块, 用于堆栈回溯
 * mov lr, pc
 * ldr pc, [r4]
 * ldmfd sp!, {r0-r12}
 * ldr lr, [sp, #4]     //恢复lr, 但不恢复sp, pc
 * add sp, sp, #12      //清除掉栈中的 {sp, lr, pc} 以及 {r0-r3}
 * ldmfd sp!, {r0-r3}   //加载可能被修改的参数
 * stmfd sp!, {r0, pc}
 * ldr r0, =100
 * sub r0, pc, r0
 * ldr r0, [r0, #8]
 * str r0, [sp, #4]     //设置old_func域中的指针为继续执行的PC
 * ldmfd sp!, {r0, pc}
 */

#define SP_BLOCK_SIZE (sizeof(SpecificHook) + sizeof(shellcodes) + sizeof (HookFuncs))
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

static char *specific_hook (void *org, void *method_obj, SpecHookFunc init_func) {
    /*  ______________
     * |__common_hook_ 0
     * |___pHooFuncs__ 4
     * |___old_func___ 8
     * |_pMonoMethod__ 12
     * |_____code[]___ 16
     */
    char *p = alloc_specific_trampo ();
    SpecificHook *shook = (SpecificHook*)p;
    shook->common_hook = common_hook;
    shook->hooks = (HookFuncs*)(p + sizeof (SpecificHook) + sizeof (shellcodes));
    memset (shook->hooks, 0, sizeof (HookFuncs));
    shook->hooks->mark = FUNC_TRACE_MARK;
    shook->hooks->func_trace = init_func;
    shook->method = method_obj;
    
    memcpy (p + sizeof (SpecificHook), shellcodes, sizeof (shellcodes));

    if (arm_hook (org, p + sizeof (SpecificHook), (void**)(&shook->old_func)) == 0)
        return 0;
    cache_flush ((uint32_t)p, (uint32_t)(p + sizeof (SpecificHook) + sizeof (shellcodes)));
    return p;
}

/*反汇编method的IL*/
static char const *disil_method (MonoMethod *method) {
    MonoMethodHeader *header = mono_method_get_header (method);
    uint32_t len = 0;
    uint8_t const *code = mono_method_header_get_code (header, &len, 0);
    return mono_disasm_code (0, method, code, code + len);
}

struct CallInfo{
    CallInfo () : order(0), times(0){}
    uint32_t order;
    uint32_t times;
};

static void send_trace_log (std::map<MonoMethod*, CallInfo> const &dict) {
    LOGD ("send_trace_log be call.");
    MemWriter sbuf(0x100000);
    MemWriter writer(0x100000);
    std::map<MonoMethod*, CallInfo>::const_iterator p;
    for (p = dict.begin (); p != dict.end (); p++) {
        MonoMethod *method = p->first;
        CallInfo call_info = p->second;
        char const *m = get_method_image_name (method);
        char *n = mono_method_full_name (method, 1);
        char str[256];
        sbuf.sprintf ("[%s] %s [%08X]|%d|%d\n", m, n, mono_method_get_token (method), call_info.times, call_info.order);
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

/*记录函数被调用的次数*/
pthread_mutex_t trace_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static void func_trace (MonoMethod *method, void *args[], ArmRegs *regs) {
    static std::map<MonoMethod*, CallInfo> trace_log;
    static uint32_t order = 0;          /*函数在该次记录过程中的调用顺序数*/
    pthread_mutex_lock (&trace_list_mutex);
    do {
        if (trace_switch == false) {
            order = 0;
            if (!trace_log.empty ()) {
                send_trace_log (trace_log);
                trace_log.clear ();
            }
            break;
        }
        /*记录过程中该函数第一次被调用时设置初始信息*/
        if (trace_log.find (method) == trace_log.end ()) {
            trace_log[method] = CallInfo ();
            trace_log[method].order = order++;
            trace_log[method].times = 1;
        }
        trace_log[method].times += 1;
    } while (0);
    pthread_mutex_unlock (&trace_list_mutex);
    return;
}

/*堆栈回溯的实现函数*/
static int walk_stack (MonoDomain *domain, MonoContext *ctx, MonoJitInfo *jit_info, void *user_data) {
    MemWriter *writer = (MemWriter*)user_data;
    MonoMethod *method = mono_jit_info_get_method (jit_info);
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

static pthread_key_t t_lua_key;
static pthread_once_t t_lua_once = PTHREAD_ONCE_INIT;

static void t_lua_free (void *p) {
    lua_State *L = (lua_State*)p;
    lua_close (L);
}

static void make_key() {
    (void) pthread_key_create (&t_lua_key, t_lua_free);
}

static void lua_hook (MonoMethod *method, void *args[], ArmRegs *regs) {
    lua_State *L = 0;
    /*每个线程一个lua_State*/
    pthread_once (&t_lua_once, make_key);
    if ((L = (lua_State*)pthread_getspecific (t_lua_key)) == 0) {
        L = lua_env_new ();
        pthread_setspecific (t_lua_key, L);
        if (L == 0) {
            xmono::LuaExecRsp rsp;
            rsp.set_level (xmono::err);
            rsp.set_message ("lua_State can not be create.");
            std::string out;
            rsp.SerializeToString (&out);
            ecmd_send (XMONO_ID_LUA_HOOK_RSP, (uint8_t const*)out.c_str (), out.size ());
            return;
        }
    }
    /*锁内拷贝是为了防止在取出代码后但执行前, 代码被其他线程释放掉*/
    pthread_mutex_lock (&lua_hook_mutex);
    char *lua_codes = 0;
    if (lua_hook_dict.find (method) != lua_hook_dict.end ()) {
        char const *p = lua_hook_dict[method];
        lua_codes = new char[strlen (p)];
        strcpy (lua_codes, p);
    }
    pthread_mutex_unlock (&lua_hook_mutex);

    /*如果出现无lua_code的情况, 不通知客户端, 直接返回*/
    if (!lua_codes)
        return;

    lua_pushlightuserdata (L, method);
    lua_setglobal (L, "method");
    lua_pushlightuserdata (L, (void*)args);
    lua_setglobal (L, "args");
    int exec_ok = !luaL_dostring (L, lua_codes);
    delete[] lua_codes;
    if (exec_ok) {
        lua_settop(L, 0);
        return;
    }

    xmono::LuaExecRsp rsp;
    rsp.set_level (xmono::err);
    rsp.set_message (lua_tostring (L, -1));
    std::string out;
    rsp.SerializeToString (&out);
    ecmd_send (XMONO_ID_LUA_HOOK_RSP, (uint8_t const*)out.c_str (), out.size ());
    lua_settop(L, 0);
    return;
}

static void common_hook (HookFuncs *hooks, MonoMethod *method, void *args[], ArmRegs *regs) {
    if (hooks->mark & FUNC_TRACE_MARK)
        hooks->func_trace (method, args, regs);
    if (hooks->mark & LUA_HOOK_MARK)
        hooks->lua_hook (method, args, regs);
    if (hooks->mark & STACK_TRACE_MARK)
        hooks->stack_trace (method, args, regs);
}

/*监控需要hook的函数*/
static void profile_jit_result (MonoProfiler *prof, MonoMethod *method, MonoJitInfo* jinfo, int result) {
    (void) prof;
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

    bool donthook = false;
    pthread_mutex_lock (&replace_mutex);
    if (replace_method_dict.find (method) != replace_method_dict.end ())
        donthook = true;
    pthread_mutex_unlock (&replace_mutex);
    if (donthook) return;

    SpecificHook *shook = (SpecificHook*)specific_hook (p, method, func_trace);
    if (shook == 0) {
        LOGD ("hook err : %s", mono_method_get_name (method));
        return;
    }
    /*TODO : 增加可配置的image和函数列表*/
    if (strcmp (get_method_image_name (method), "Assembly-CSharp") != 0 ||
        strcmp (mono_method_get_name (method), ".ctor") == 0 ||
        strcmp (mono_method_get_name (method), ".cctor") == 0 ||
        strcmp (mono_method_get_name (method), "set") == 0 ||
        strcmp (mono_method_get_name (method), "get") == 0 ||
        strcmp (mono_method_get_name (method), "Update") == 0 ||
        strcmp (mono_method_get_name (method), "LateUpdate") == 0 ||
        strcmp (mono_method_get_name (method), "OnGUI") == 0) {
        shook->hooks->mark &= (~FUNC_TRACE_MARK);
    }
    pthread_mutex_lock (&hooked_mutex);
    hooked_method_dict[method] = new HookInfo(jinfo, (SpecificHook*)shook);
    pthread_mutex_unlock (&hooked_mutex);
    return;
}

/*mono正常退出时 该函数被调用*/
static void mono_shutdown (MonoProfiler *prof) {
    (void) prof;
    LOGD ("mono over.");
}

static void install_jit_profile () {
    mono_profiler_install (0, mono_shutdown);
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

/*这个函数名真销魂*/
static bool hook_if_method_not_hook (MonoMethod *method) {
    pthread_mutex_lock (&hooked_mutex);
    bool is_hooked = hooked_method_dict.find (method) != hooked_method_dict.end ();
    pthread_mutex_unlock (&hooked_mutex);
    if (is_hooked)
        return true;

    /*Fixme : 这里直接使用了0 号domain, 一般unity游戏中只有这一个, 但不能保证特殊情况*/
    MonoThread *thred = mono_thread_attach (mono_domain_get_by_id (0));
    mono_compile_method (method);
    mono_thread_detach (thred);

    pthread_mutex_lock (&hooked_mutex);
    is_hooked = hooked_method_dict.find (method) != hooked_method_dict.end ();
    pthread_mutex_unlock (&hooked_mutex);
    return is_hooked;
}

static void unset_stack_trace (Package *pkg) {
    xmono::UnStackTraceReq req;
    std::string str((char*)pkg->body, pkg->all_len - sizeof (Package));
    if (!req.ParseFromString (str)) {
        LOGD ("xmono::StackTraceReq ParseFromString err!");
        return;
    }
    MonoMethod *method = get_method_with_token(req.image_name ().c_str (), req.method_token ());
    if (!method) {
        return;
    }
    pthread_mutex_lock (&hooked_mutex);
    if (hooked_method_dict.find (method) != hooked_method_dict.end ()) {
        HookInfo *info = hooked_method_dict[method];
        SpecificHook *shook = (SpecificHook*)info->specific_hook;
        shook->hooks->mark &= (~STACK_TRACE_MARK);
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
        MonoMethod *method = get_method_with_token(req.image_name ().c_str (), req.method_token ());
        if (!method) {
            rsp.set_err (false);
            rsp.set_err_str (helper_last_err ());
            break;
        }
        if (!hook_if_method_not_hook (method)) {
            rsp.set_err (false);
            rsp.set_err_str ("this method canot be hook.");
            break;
        }
        pthread_mutex_lock (&hooked_mutex);
        HookInfo *info = hooked_method_dict[method];
        SpecificHook *shook = (SpecificHook*)info->specific_hook;
        shook->hooks->stack_trace = stack_trace;
        shook->hooks->mark |= STACK_TRACE_MARK;
        pthread_mutex_unlock (&hooked_mutex);
        rsp.set_err (true);
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
    MonoDomain *domain;
    MonoMethod * method = get_method_with_token (req.image_name ().c_str (), req.method_token ());
    if (!method) {
        rsp.set_err (false);
        rsp.set_msg (helper_last_err ());
        goto replace_method_end;
    }
    if (!hook_if_method_not_hook (method)) {
        rsp.set_err (false);
        rsp.set_msg ("this method can not be hook.");
        goto replace_method_end;
    }
    domain = mono_domain_get_by_id (req.domain_id ());
    if (!domain) {
        rsp.set_err (false);
        rsp.set_msg ("can not get the domain from id");
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

    /*不用判断失败是因为失败会直接导致异常*/
    p = mono_compile_method (new_method);
    pthread_mutex_lock (&hooked_mutex);
    memcpy (&hooked_method_dict[method]->specific_hook->old_func, &p, 4);
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
    std::string err("");
    MonoMethod *method = get_method_with_token (req.image_name ().c_str (), req.method_token ());
    if (!method) err += helper_last_err ();
    MonoImage *image = mono_image_loaded (req.image_name ().c_str ());
    if (!image) err += "  image : " + req.image_name () + " can not be find!";
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

static void lua_code_exec (Package *pkg) {
    static lua_State *L = 0;
    xmono::LuaExecRsp rsp;
    xmono::LuaExecReq req;
    MonoThread *thread = mono_thread_attach (mono_domain_get_by_id (0));
    do {
        if (L == 0 && !(L = lua_env_new ())) {
            rsp.set_level (xmono::err);
            rsp.set_message ("create a lua_State err.");
            break;
        }
        std::string str((char*)pkg->body, pkg->all_len - sizeof (Package)); //Fixme : 修复头部all_len是总长的问题
        if (!req.ParseFromString (str)) {
            LOGD ("xmono::LuaExecReq ParseFromString err!");
            return;
        }
        if (luaL_dostring (L, req.lua_code ().c_str ())) {
            rsp.set_level (xmono::err);
            rsp.set_message (lua_tostring (L, -1));
            break;
        }
        rsp.set_level (xmono::debug);
        rsp.set_message ("exec the lua string ok.");
    } while (0);
    mono_thread_detach (thread);
    std::string out;
    rsp.SerializeToString (&out);
    ecmd_send (XMONO_ID_LUA_EXEC_RSP, (uint8_t const*)out.c_str (), out.size ());
    return;
}

static void hook_with_lua (Package *pkg) {
    xmono::LuaHookReq req;
    xmono::LuaHookRsp rsp;
    std::string str((char*)pkg->body, pkg->all_len - sizeof (Package));
    if (!req.ParseFromString (str)) {
        LOGD ("xmono::LuaHookReq ParseFromString err!");
        return;
    }
    do {
        MonoMethod *method = get_method_with_token (req.image_name ().c_str (), req.method_token ());
        if (!method) {
            rsp.set_level (xmono::err);
            rsp.set_message ("method can not be find.");
            break;
        }
        if (req.lua_code ().empty ()) {
            rsp.set_level (xmono::err);
            rsp.set_message ("lua_code must not be nil.");
            break;
        }
        if (!hook_if_method_not_hook (method)) {
            rsp.set_level (xmono::err);
            rsp.set_message ("this method can not be hook.");
            break;
        }

        if (req.disable ()) {
            pthread_mutex_lock (&hooked_mutex);
            SpecificHook *shook = hooked_method_dict[method]->specific_hook;
            shook->hooks->mark &= (~LUA_HOOK_MARK);
            pthread_mutex_unlock (&hooked_mutex);
            break;
        }

        char *p = new char[req.lua_code ().size () + 1];
        memcpy (p, req.lua_code ().c_str (), req.lua_code ().size () + 1);

        pthread_mutex_lock (&hooked_mutex);
        SpecificHook *shook = hooked_method_dict[method]->specific_hook;
        shook->hooks->lua_hook = lua_hook;
        shook->hooks->mark |= LUA_HOOK_MARK;
        pthread_mutex_unlock (&hooked_mutex);

        pthread_mutex_lock (&lua_hook_mutex);
        if (lua_hook_dict.find (method) != lua_hook_dict.end ())
            delete[] lua_hook_dict[method];
        if (req.disable ())
            lua_hook_dict.erase (method);
        else
            lua_hook_dict[method] = p;
        pthread_mutex_unlock (&lua_hook_mutex);

        rsp.set_level (xmono::info);
        rsp.set_message ("hook_with_lua completion.");
    } while (0);
    std::string out;
    rsp.SerializeToString (&out);
    ecmd_send (XMONO_ID_LUA_HOOK_RSP, (uint8_t const*)out.c_str (), out.size ());
    return;
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
    ecmd_register_resp (XMONO_ID_LUA_EXEC_REP, lua_code_exec);
    ecmd_register_resp (XMONO_ID_LUA_HOOK_REP, hook_with_lua);
    LOGD ("ecmd init over.");
}

/*获取全局变量里面的method*/
static MonoMethod *get_global_method (lua_State *L) {
    lua_getglobal (L, "method");
    MonoMethod *method = (MonoMethod*)lua_touserdata (L, -1);
    lua_pop (L, 1);
    if (!method)
        luaL_error (L, "[luahook] : method is nil!");
    return method;
}

/*获取全局变量里面的args*/
static void **get_global_args (lua_State *L) {
    lua_getglobal (L, "args");
    void **args = (void**)lua_touserdata (L, -1);
    lua_pop (L, 1);
    if (!args)
        luaL_error (L, "[luahook] : args is nil!");
    return args;
}

static int l_log (lua_State *L) {
    lua_getglobal (L, "string");
    lua_getfield (L, -1, "format");
    lua_insert (L, 1);
    lua_pop (L, 1);
    if (lua_pcall (L, lua_gettop (L) - 1, 1, 0) != LUA_OK)
        return lua_error (L);
    LOGD ("[lua log] : %s", luaL_checkstring (L, -1));
    return 0;
}

static int l_get_args (lua_State *L) {
    int offset = 0;
    int index = luaL_checkint (L, 1) - 1;
    if (index < 0)
        luaL_error (L, "[luahook] : index must be greater than 0.");

    MonoMethod *method = get_global_method (L);
    void **args = get_global_args (L);
    /*非static函数, offset+4, 绕过this指针*/
    uint32_t iflags;
    int flag = mono_method_get_flags (method, &iflags);
    if (!(flag & MONO_METHOD_ATTR_STATIC))
        offset = 4;

    MonoMethodSignature *sig = mono_method_signature (method);
    void *iter = 0;
    for (int i = 0; i < index; i++) {
        MonoType *param_type = mono_signature_get_params (sig, &iter);
        switch (mono_type_get_type (param_type)) {
            case MONO_TYPE_U8:
            case MONO_TYPE_I8:
            case MONO_TYPE_R8:
                offset += 8;
            default:
                offset += 4;
        }
    }

    MonoType *dest_type = mono_signature_get_params (sig, &iter);
    void *val = &args[offset / 4];
    if (mono_type_is_reference (dest_type)) {
        lua_pushlightuserdata (L, *(void**)val);
    } else {
        switch (mono_type_get_type (dest_type)) {
            case MONO_TYPE_VOID:
                lua_pushnil (L);
                break;
            case MONO_TYPE_BOOLEAN:
                lua_pushboolean (L, *(bool*)val);
                break;
            case MONO_TYPE_U2:
            case MONO_TYPE_CHAR:
                /*char 用int来表示*/
                lua_pushinteger (L, *(uint16_t*)val);
                break;
            case MONO_TYPE_I1:
                lua_pushinteger(L, *(int8_t*)val);
                break;
            case MONO_TYPE_U1:
                lua_pushinteger(L, *(uint8_t*)val);
                break;
            case MONO_TYPE_I2:
                lua_pushinteger (L, *(int16_t*)val);
                break;
            case MONO_TYPE_I4:
                lua_pushinteger(L, *(int32_t*)val);
                break;
            case MONO_TYPE_VALUETYPE:
            case MONO_TYPE_U4:
                lua_pushinteger(L, *(uint32_t*)val);
                break;
            case MONO_TYPE_I8:
            case MONO_TYPE_U8: {
                memcpy (lua_newuserdata (L, sizeof (int64_t)), val, sizeof (int64_t));
                break;
            }
            case MONO_TYPE_R4:
                lua_pushnumber(L, *(float*)val);
                break;
            case MONO_TYPE_R8:
                lua_pushnumber(L, *(double*)val);
                break;
            case MONO_TYPE_I:
            case MONO_TYPE_U:
                luaL_error (L, "donot support the intptr & uintptr.");
            case MONO_TYPE_MVAR:
                luaL_error (L, "generic method dont be supported.");
            case MONO_TYPE_PTR:
                luaL_error (L, "dont support the ptr type.");
            default:
                luaL_error (L, "unknow method args type : 0x%02X", mono_type_get_type (dest_type));
        }
    }
    return 1;
}

static int l_get_this_pointer (lua_State *L) {
    MonoMethod *method = get_global_method (L);
    /*static函数无this指针*/
    uint32_t iflags;
    int flag = mono_method_get_flags (method, &iflags);
    if (flag & MONO_METHOD_ATTR_STATIC)
        return luaL_error (L, "static method not have this pointer.");

    void **args = get_global_args (L);
    lua_pushlightuserdata (L, args[0]);
    return 1;
}

static int l_set_args (lua_State *L) {
    if (lua_gettop (L) != 2)
        return luaL_error (L, "need 2 args. arg1 : index, arg2 : value.");
    int index = luaL_checkint (L, 1) - 1;

    int offset = 0;
    MonoMethod *method = get_global_method (L);
    uint32_t iflags;
    int flag = mono_method_get_flags (method, &iflags);
    if (!(flag & MONO_METHOD_ATTR_STATIC))
        offset = 4;

    MonoMethodSignature *sig = mono_method_signature (method);
    int arg_num = mono_signature_get_param_count (sig);
    if (index >= arg_num)
        return luaL_error (L, "index is greater than method's params count.");

    void *iter = 0;
    for (int i = 0; i < index; i++) {
        MonoType *param_type = mono_signature_get_params (sig, &iter);
        switch (mono_type_get_type (param_type)) {
            case MONO_TYPE_U8:
            case MONO_TYPE_I8:
            case MONO_TYPE_R8:
                offset += 8;
            default:
                offset += 4;
        }
    }
    void **args = get_global_args (L);
    MonoType *dest_type = mono_signature_get_params (sig, &iter);
    void *val = &args[offset / 4];
    if (mono_type_is_reference (dest_type)) {
        *(void**)val = lua_touserdata (L, 2);
    } else {
        switch (mono_type_get_type (dest_type)) {
            case MONO_TYPE_BOOLEAN:
                *(bool*)val = lua_toboolean (L, 2);
                break;
            case MONO_TYPE_U2:
            case MONO_TYPE_CHAR:
            case MONO_TYPE_U1:
            case MONO_TYPE_VALUETYPE:
            case MONO_TYPE_U4:
                *(int32_t*)val = luaL_checkint (L, 2);
                break;
            case MONO_TYPE_I1:
            case MONO_TYPE_I2:
            case MONO_TYPE_I4:
                *(uint32_t*)val = luaL_checkunsigned (L, 2);
                break;
            case MONO_TYPE_I8:
            case MONO_TYPE_U8: {
                void *p = lua_touserdata (L, 2);
                if (!p)
                    return luaL_error (L, "64 bit number is nil.");
                memcpy (val, p, sizeof (int64_t));
                break;
            }
            case MONO_TYPE_R4:
                *(float*)val = (float)lua_tonumber (L, 2);
                break;
            case MONO_TYPE_R8:
                *(double*)val = lua_tonumber (L, 2);
                break;
            case MONO_TYPE_I:
            case MONO_TYPE_U:
                luaL_error (L, "donot support the intptr & uintptr.");
            case MONO_TYPE_MVAR:
                luaL_error (L, "generic method dont be supported.");
            case MONO_TYPE_PTR:
                luaL_error (L, "dont support the ptr type.");
            default:
                luaL_error (L, "unknow method args type : 0x%02X", mono_type_get_type (dest_type));
        }
    }
    return 0;
}

static const luaL_Reg R[] = {
    {"log", l_log},
    {"get_args", l_get_args},
    {"get_this_pointer", l_get_this_pointer},
    {"set_args", l_set_args},
    {0, 0}
};

int luaopen_xmono (lua_State *L) {
    luaL_newlib (L, R);
    return 1;
}

static lua_State *lua_env_new () {
    lua_State *L = luaL_newstate ();
    luaL_openlibs (L);
    char const *rpath_code = "package.cpath = package.cpath .. '/data/local/tmp/xmono/lualibs/lib?.so'\n"
                             "package.path = package.path .. '/data/local/tmp/xmono/lualibs/?.lua'\n";
    if (luaL_dostring (L, rpath_code)) {
        char const *err = lua_tostring (L, -1);
        LOGE ("%s", err);
        lua_close (L);
        return 0;
    }
    luaL_requiref (L, "mono", luaopen_mono, 1);
    lua_pop (L, 1);
    luaL_requiref (L, "xmono", luaopen_xmono, 1);
    lua_pop (L, 1);
    return L;
}

extern "C" int so_main() {
    LOGD ("hello, xmono");
    install_jit_profile ();
    init_network ();
    return 0;
}