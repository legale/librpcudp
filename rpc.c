#include "rpc.h"
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

// constants
#define RPC_MAX_PACKET_SIZE 4096
#define RPC_DEFAULT_TIMEOUT_SEC 5

/* Structure to track client requests */
typedef struct {
  struct sockaddr_in addr;
  socklen_t addr_len;
} client_info_t;

/* Global context */
static rpc_context_t g_ctx = {0};

/* Simple logging macro */
#define RPC_LOG(fmt, ...)                                                      \
  fprintf(stderr, "%s: " fmt "\n", __func__, ##__VA_ARGS__)

/* Function implementations */
const char *hello_func(int32_t argc, char **argv) {
  RPC_LOG("argc=%d", argc);
  static char hello_buffer[RPC_BUFFER_SIZE];
  size_t pos = 0;
  int32_t i;
  int32_t len;
  size_t remaining;

  /* Basic message */
  remaining = sizeof(hello_buffer) - pos;
  if (remaining <= 1) {
    return "error: buffer overflow";
  }

  len = snprintf(hello_buffer + pos, remaining, "world");
  if (len < 0 || (size_t)len >= remaining) {
    return "error: buffer overflow";
  }
  pos += (size_t)len;

  /* Add arguments if present */
  if (argc > 0 && argv != NULL) {
    remaining = sizeof(hello_buffer) - pos;
    if (remaining <= 1) {
      return hello_buffer; /* Return what we have so far */
    }

    len = snprintf(hello_buffer + pos, remaining, " (argc=%d", argc);
    if (len < 0 || (size_t)len >= remaining) {
      return hello_buffer; /* Return what we have so far */
    }
    pos += (size_t)len;

    /* Add each argument */
    for (i = 0; i < argc; i++) {
      if (argv[i] == NULL) {
        continue;
      }

      remaining = sizeof(hello_buffer) - pos;
      if (remaining <= 1) {
        break;
      }

      len =
          snprintf(hello_buffer + pos, remaining, " argv[%d]='%s'", i, argv[i]);
      if (len < 0 || (size_t)len >= remaining) {
        break;
      }
      pos += (size_t)len;
    }

    /* Close parenthesis */
    remaining = sizeof(hello_buffer) - pos;
    if (remaining > 1) {
      len = snprintf(hello_buffer + pos, remaining, ")");
      if (len > 0 && (size_t)len < remaining) {
        pos += (size_t)len;
      }
    }
  }

  return hello_buffer;
}

const char *stop_func(int32_t argc, char **argv) {
  /* Intentionally unused parameters - documented */
  (void)argc;
  (void)argv;
  atomic_store(&g_ctx.keep_running, false);
  return "0";
}

const char *echo_func(int32_t argc, char **argv) {
  size_t pos = 0;
  int32_t i;
  int32_t len;
  size_t remaining;

  if (argv == NULL) {
    return "error: invalid arguments";
  }

  /* Add argument count */
  remaining = sizeof(g_ctx.echo_buffer) - pos;
  if (remaining <= 1) {
    return "error: buffer overflow";
  }

  len = snprintf(g_ctx.echo_buffer + pos, remaining, "argc=%d", argc);
  if (len < 0 || (size_t)len >= remaining) {
    return "error: buffer overflow";
  }
  pos += (size_t)len;

  /* Add each argument */
  for (i = 0; i < argc; i++) {
    if (argv[i] == NULL) {
      continue;
    }

    remaining = sizeof(g_ctx.echo_buffer) - pos;
    if (remaining <= 1) {
      break;
    }

    len = snprintf(g_ctx.echo_buffer + pos, remaining, " argv[%d]='%s'", i,
                   argv[i]);
    if (len < 0 || (size_t)len >= remaining) {
      break;
    }
    pos += (size_t)len;
  }

  return g_ctx.echo_buffer;
}

int32_t rpc_register(const char *name, rpc_cb func) {
  /* For API compatibility, not used in this example */
  if ((name == NULL) || (func == NULL)) {
    return RPC_ERROR;
  }
  return RPC_ERROR;
}

int32_t register_str_func(const char *name, rpc_string_cb func) {
  size_t name_len;

  if ((name == NULL) || (func == NULL)) {
    return RPC_ERROR;
  }

  if (g_ctx.function_count >= MAX_FUNCTIONS) {
    return RPC_ERROR;
  }

  name_len = strlen(name);
  if (name_len >= sizeof(g_ctx.functions[0].name)) {
    name_len = sizeof(g_ctx.functions[0].name) - 1;
  }

  memcpy(g_ctx.functions[g_ctx.function_count].name, name, name_len);
  g_ctx.functions[g_ctx.function_count].name[name_len] = '\0';
  g_ctx.functions[g_ctx.function_count].func = func;
  g_ctx.function_count++;

  return RPC_SUCCESS;
}

static const char *call_function(const char *name, int32_t argc, char **argv) {
  if (name == NULL) {
    return "-1";
  }

  for (uint32_t i = 0; i < g_ctx.function_count; i++) {
    if (strcmp(name, g_ctx.functions[i].name) == 0) {
      if (g_ctx.functions[i].func != NULL) {
        return g_ctx.functions[i].func(argc, argv);
      }
    }
  }

  /* Call echo with the new set of arguments */
  return echo_func(argc, argv);
}

static void send_result(const char *result, const client_info_t *client) {
  char buffer[RPC_MAX_PACKET_SIZE];
  int32_t len;
  ssize_t sent_bytes;

  if (client == NULL) {
    return;
  }

  if (result == NULL) {
    result = "";
  }

  len = snprintf(buffer, sizeof(buffer), "%s", result);
  if ((len < 0) || ((size_t)len >= sizeof(buffer))) {
    return;
  }

  sent_bytes = sendto(g_ctx.sock_fd, buffer, (size_t)len, 0,
                      (struct sockaddr *)&client->addr, client->addr_len);
  if (sent_bytes < 0) {
    RPC_LOG("error send res error='%s'", strerror(errno));
  }
}

static int32_t parse_args(char *buffer, size_t bufsize, int32_t *argc_ptr,
                          char ***argv_ptr, size_t argv_size) {
  size_t arg_count = 0;
  char **argv;
  const char *p;
  const char *end;
  const char *check;

  if ((buffer == NULL) || (argc_ptr == NULL) || (argv_ptr == NULL)) {
    return EINVAL;
  }

  /* Check that buffer size is valid */
  if (bufsize == 0) {
    return EINVAL;
  }

  argv = *argv_ptr;
  p = buffer;
  end = buffer + bufsize;

  while ((p < end) && (arg_count < argv_size - 1)) {
    /* Skip consecutive zeros (find argument start) */
    while (p < end && *p == '\0') {
      p++;
    }
    if (p >= end) {
      break; /* Reached end of buffer */
    }

    /* Record argument start */
    argv[arg_count] = (char *)p;
    arg_count++;

    /* Skip to next null or end of buffer */
    while (p < end && *p != '\0') {
      p++;
    }

    /* Break if reached end of buffer */
    if (p >= end) {
      break;
    }
  }

  /* Check if there are unprocessed arguments in buffer */
  if (p < end) {
    /* Check if there are any non-null chars left */
    check = p;
    while (check < end && *check == '\0') {
      check++;
    }
    if (check < end) {
      return EOVERFLOW;
    }
  }

  /* Null-terminate the argv array */
  argv[arg_count] = NULL;

  /* Check for integer overflow in argc */
  if (arg_count > (size_t)INT_MAX) {
    return EINVAL;
  }

  *argc_ptr = (int32_t)arg_count;
  return 0;
}

static int32_t rpc_handle_request(char *buffer, ssize_t recv_size,
                                  client_info_t *client) {
  char *argv[MAX_ARGS];
  char **argv_ptr = argv;
  int32_t argc = 0;
  const char *result;
  int32_t parse_result;

  if (buffer == NULL || client == NULL) {
    return RPC_ERROR;
  }

  RPC_LOG("recv_size=%zd", recv_size);

  /* Validate received size */
  if (recv_size < 0 || recv_size >= RPC_MAX_PACKET_SIZE) {
    return RPC_ERROR;
  }

  /* Null-terminate the buffer */
  buffer[recv_size] = '\0';

  /* Parse arguments */
  parse_result =
      parse_args(buffer, (size_t)recv_size, &argc, &argv_ptr, MAX_ARGS);
  if (parse_result != 0) {
    RPC_LOG("error parsing arguments res=%d", parse_result);
    return RPC_ERROR;
  }

  /* Call the function if we have at least one argument (function name) */
  if (argc > 0 && argv[0] != NULL) {
    RPC_LOG("call func=%s argc=%d", argv[0], argc - 1);
    result = call_function(argv[0], argc - 1, &argv[1]);
    send_result(result, client);
    return RPC_SUCCESS;
  }

  return RPC_ERROR;
}

static void *rpc_server_thread(void *arg) {
  fd_set read_fds;
  int32_t ready;
  char buffer[RPC_MAX_PACKET_SIZE];
  ssize_t recv_len;
  client_info_t client;
  struct timespec timeout;

  /* Avoid unused parameter warning */
  (void)arg;

  while (atomic_load(&g_ctx.keep_running)) {
    /* Initialize variables for each iteration */
    timeout.tv_sec = 1;
    timeout.tv_nsec = 0;
    client.addr_len = sizeof(client.addr);

    /* Set up select */
    FD_ZERO(&read_fds);
    FD_SET(g_ctx.sock_fd, &read_fds);

    ready = pselect(g_ctx.sock_fd + 1, &read_fds, NULL, NULL, &timeout, NULL);

    /* Handle select result */
    if (ready < 0) {
      if (errno == EINTR) {
        continue; /* Interrupted by signal */
      }
      RPC_LOG("error in pselect error='%s'", strerror(errno));
      break;
    }

    if (ready > 0 && FD_ISSET(g_ctx.sock_fd, &read_fds)) {
      /* Clear buffer before receiving data */
      memset(buffer, 0, sizeof(buffer));

      /* Receive data */
      recv_len = recvfrom(g_ctx.sock_fd, buffer, sizeof(buffer) - 1, 0,
                          (struct sockaddr *)&client.addr, &client.addr_len);

      /* Process received data */
      if (recv_len <= 0) {
        RPC_LOG("error in recvfrom: %s", strerror(errno));
        continue;
      }

      /* Handle the request */
      RPC_LOG("buf=%zd '%s'", recv_len, buffer);
      rpc_handle_request(buffer, recv_len, &client);
    }
  }

  return NULL;
}

int32_t rpc_client_call(const char *server_ip, int32_t port, int32_t argc,
                        char **argv, char *response, size_t response_size) {
  int32_t client_sock;
  struct sockaddr_in server_addr;
  socklen_t server_len = sizeof(server_addr);
  ssize_t bytes_sent, bytes_received;
  struct timeval tv;
  char request_buffer[RPC_MAX_PACKET_SIZE];
  int32_t i = 0;
  size_t pos = 0;
  int32_t len;
  size_t remaining;

  /* Parameter validation */
  if (argc < 1 || argv == NULL || server_ip == NULL || response == NULL ||
      response_size == 0) {
    return RPC_ERROR;
  }

  /* Build request string with null-byte delimiters */
  for (i = 0; i < argc; i++) {
    if (argv[i] == NULL) {
      continue;
    }

    remaining = RPC_MAX_PACKET_SIZE - pos - 1; /* -1 for final null */
    if (remaining <= 1) {
      break;
    }

    /* Copy argument followed by null terminator */
    len = snprintf(request_buffer + pos, remaining, "%s%c", argv[i], '\0');
    if (len < 0 || (size_t)len >= remaining) {
      break;
    }
    pos += (size_t)len;
  }

  /* Ensure the request is properly terminated */
  if (pos < RPC_MAX_PACKET_SIZE) {
    request_buffer[pos] = '\0';
  } else {
    request_buffer[RPC_MAX_PACKET_SIZE - 1] = '\0';
  }

  /* Create UDP socket */
  client_sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (client_sock < 0) {
    RPC_LOG("error create socket error='%s'", strerror(errno));
    return RPC_ERROR;
  }

  /* Set socket timeout */
  tv.tv_sec = RPC_DEFAULT_TIMEOUT_SEC;
  tv.tv_usec = 0;
  if (setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    RPC_LOG("error set socket error='%s'", strerror(errno));
    close(client_sock);
    return RPC_ERROR;
  }

  /* Configure server address */
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons((uint16_t)port);

  if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
    RPC_LOG("invalid ipv4=%s", server_ip);
    close(client_sock);
    return RPC_ERROR;
  }

  /* Send request to server */
  bytes_sent = sendto(client_sock, request_buffer, pos, 0,
                      (struct sockaddr *)&server_addr, server_len);
  if (bytes_sent < 0) {
    RPC_LOG("error send error='%s'", strerror(errno));
    close(client_sock);
    return RPC_ERROR;
  }

  /* Receive response from server */
  bytes_received = recvfrom(client_sock, response, response_size - 1, 0,
                            (struct sockaddr *)&server_addr, &server_len);

  close(client_sock);

  if (bytes_received < 0) {
    RPC_LOG("error recv res='%s'", strerror(errno));
    return RPC_ERROR;
  }

  /* Null-terminate the response */
  response[bytes_received] = '\0';
  return RPC_SUCCESS;
}

rpc_context_t *rpc_init(void) {
  struct sockaddr_in server_addr;

  /* Initialize the keep_running flag */
  atomic_store(&g_ctx.keep_running, true);

  /* Create UDP socket */
  g_ctx.sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (g_ctx.sock_fd < 0) {
    RPC_LOG("error create socket: %s", strerror(errno));
    return NULL;
  }

  /* Configure server address */
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(DEFAULT_RPC_PORT);

  /* Bind socket to address */
  if (bind(g_ctx.sock_fd, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {
    RPC_LOG("error bind socket: %s", strerror(errno));
    close(g_ctx.sock_fd);
    g_ctx.sock_fd = -1;
    return NULL;
  }

  /* Start server thread */
  if (pthread_create(&g_ctx.server_thread, NULL, rpc_server_thread, NULL) !=
      0) {
    RPC_LOG("error create server thread: %s", strerror(errno));
    close(g_ctx.sock_fd);
    g_ctx.sock_fd = -1;
    return NULL;
  }

  return &g_ctx;
}

int rpc_deinit(rpc_context_t *ctx) {
  if (ctx == NULL) {
    return EINVAL;
  }

  // check if rpc server is running
  if (!atomic_load(&ctx->keep_running)) {
    return EINVAL;
  }
  /* Signal the server thread to exit */
  atomic_store(&ctx->keep_running, false);

  /* Join the server thread */
  if (pthread_join(ctx->server_thread, NULL) != 0) {
    RPC_LOG("Failed to join server thread: %s", strerror(errno));
  }

  /* Close the socket */
  if (ctx->sock_fd >= 0) {
    close(ctx->sock_fd);
    ctx->sock_fd = -1;
  }
  return 0;
}
