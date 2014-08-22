#ifndef ECMD_H
#define ECMD_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _Package Package;
typedef struct _RespCallback RespCallback;
typedef void (*RespCallbackFunc) (Package*);
typedef void (*ErrCallbackFunc) ();

struct _Package {
    uint32_t all_len;
    uint32_t cmd_id;
    uint8_t body[];
};


char const *ecmd_err_str ();
int ecmd_init (ErrCallbackFunc func);
int ecmd_start_client (char const *ip, uint16_t port);
int ecmd_start_server (char const *ip, uint16_t port);
void ecmd_send (uint32_t cmd_id, uint8_t const *data, size_t len);
void ecmd_register_resp (uint32_t cmd_id, RespCallbackFunc func);
void ecmd_unregister_resp (uint32_t cmd_id);

#ifdef __cplusplus
}
#endif

#endif