#ifndef RPC_H
#define RPC_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L /* For proper POSIX compliance */
#endif

#ifndef __USE_MISC
#define __USE_MISC
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/types.h>
#include <time.h>

/* Constants */
#define MAX_FUNCTIONS 10
#define MAX_ARGS 10
#define MAX_LINE_LENGTH 256
#define DEFAULT_RPC_PORT 8888
#define MAX_PACKET_SIZE 4096
#define RPC_BUFFER_SIZE 2048

/* Status codes */
#define RPC_SUCCESS 0
#define RPC_ERROR (-1)

/* Function typedefs */
typedef int32_t (*rpc_cb)(int32_t argc, char **argv, char *out);
typedef const char *(*rpc_string_cb)(int32_t argc, char **argv);

/* Error types */
typedef enum {
  RPC_ERR_NONE = 0,
  RPC_ERR_INVALID_PARAM,
  RPC_ERR_BUFFER_OVERFLOW,
  RPC_ERR_NETWORK,
  RPC_ERR_MEMORY,
  RPC_ERR_SYSTEM
} rpc_error_t;

/* Function types and structures */
typedef const char *(*rpc_string_cb)(int32_t argc, char **argv);

typedef struct {
  char name[50];
  rpc_string_cb func;
} rpc_func_t;

typedef struct {
  rpc_func_t functions[MAX_FUNCTIONS];
  uint32_t function_count;
  int sock_fd;
  atomic_bool keep_running;
  pthread_t server_thread;
  char echo_buffer[RPC_BUFFER_SIZE];
} rpc_context_t;

/**
 * Initialize the RPC server
 *
 * @return rpc_context_t * on success, NULL on failure
 */
rpc_context_t *rpc_init(void);

/**
 * Clean up and shut down the RPC server
 */
int rpc_deinit(rpc_context_t *ctx);

/**
 * Register a function callback with the RPC server
 *
 * @param name Function name to register
 * @param func Function callback to call when name is invoked
 * @return RPC_SUCCESS on success, RPC_ERROR on failure
 */
int32_t rpc_register(const char *name, rpc_cb func);

/**
 * Register a string function callback with the RPC server
 *
 * @param name Function name to register
 * @param func Function callback to call when name is invoked
 * @return RPC_SUCCESS on success, RPC_ERROR on failure
 */
int32_t register_str_func(const char *name, rpc_string_cb func);

/* Example default commands */

/**
 * @param argc Argument count
 * @param argv Argument array
 * @return string
 */
const char *echo_func(int32_t argc, char **argv);
const char *hello_func(int32_t argc, char **argv);
const char *stop_func(int32_t argc, char **argv);

/**
 * Send an RPC request to a server and wait for a response
 *
 * @param server_ip IP address of the RPC server
 * @param port Server port number
 * @param argc Number of arguments (including function name)
 * @param argv Array of arguments (argv[0] is function name)
 * @param response Buffer to store response
 * @param response_size Size of response buffer
 * @return RPC_SUCCESS on success, RPC_ERROR on failure
 */
int32_t rpc_client_call(const char *server_ip, int32_t port, int32_t argc,
                        char **argv, char *response, size_t response_size);

#endif /* RPC_H */