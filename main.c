#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rpc.h"

/* Safe buffer for result */
#define RESULT_BUFFER_SIZE 64

/* Flag to indicate server shutdown */
static volatile sig_atomic_t server_running = 1;

/**
 * Signal handler for graceful shutdown
 *
 * @param sig Signal received
 */
static void handle_signal(int sig) {
  if (sig == SIGINT || sig == SIGTERM) {
    server_running = 0;
  }
}

/**
 * Add function implementation - adds two integers
 *
 * @param argc Argument count
 * @param argv Argument array
 * @return String containing the sum or error code
 */
const char *add_func(int32_t argc, char **argv) {
  static char result[RESULT_BUFFER_SIZE];
  int32_t num1, num2, sum;
  int32_t ret;

  /* Clear result buffer */
  memset(result, 0, sizeof(result));

  /* Validate arguments */
  if (argc != 2 || argv == NULL || argv[0] == NULL || argv[1] == NULL) {
    ret = snprintf(result, sizeof(result), "-2");
    if (ret < 0 || (size_t)ret >= sizeof(result)) {
      return "Error: Buffer overflow";
    }
    return result;
  }

  /* Convert and add numbers */
  num1 = strtol(argv[0], NULL, 10);
  num2 = strtol(argv[1], NULL, 10);
  sum = num1 + num2;

  /* Format result */
  ret = snprintf(result, sizeof(result), "%d", sum);
  if (ret < 0 || (size_t)ret >= sizeof(result)) {
    return "Error: Buffer overflow";
  }

  return result;
}

/**
 * Main function - acts as both client and server
 *
 * @param argc Argument count
 * @param argv Argument array
 * @return Exit status code
 */
int main(int argc, char **argv) {
  /* Client mode when arguments are provided */
  if (argc > 1) {
    char response[MAX_PACKET_SIZE];
    int32_t ret;

    /* Make RPC call using the client function */
    printf("Sending request: function '%s' with %d arguments\n", argv[1],
           argc - 2);

    ret = rpc_client_call("127.0.0.1", DEFAULT_RPC_PORT, argc - 1, &argv[1],
                          response, sizeof(response));

    if (ret == RPC_SUCCESS) {
      printf("%s\n", response);
      return EXIT_SUCCESS;
    } else {
      fprintf(stderr, "Error: Failed to get response from server\n");
      return EXIT_FAILURE;
    }
  }
  /* Server mode when no arguments are provided */
  else {
    struct sigaction sa;
    const char *func_name;
    int32_t ret;

    /* Set up signal handler for graceful shutdown */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1 ||
        sigaction(SIGTERM, &sa, NULL) == -1) {
      perror("Failed to set up signal handler");
      return EXIT_FAILURE;
    }

    /* Register functions */
    func_name = "add";
    ret = register_str_func(func_name, add_func);
    if (ret != RPC_SUCCESS) {
      fprintf(stderr, "Failed to register %s function\n", func_name);
      return EXIT_FAILURE;
    }

    func_name = "hello";
    ret = register_str_func(func_name, hello_func);
    if (ret != RPC_SUCCESS) {
      fprintf(stderr, "Failed to register %s function\n", func_name);
      return EXIT_FAILURE;
    }

    func_name = "echo";
    ret = register_str_func(func_name, echo_func);
    if (ret != RPC_SUCCESS) {
      fprintf(stderr, "Failed to register %s function\n", func_name);
      return EXIT_FAILURE;
    }

    func_name = "stop";
    ret = register_str_func(func_name, stop_func);
    if (ret != RPC_SUCCESS) {
      fprintf(stderr, "Failed to register %s function\n", func_name);
      return EXIT_FAILURE;
    }

    /* Initialize RPC server */
    printf("starting rpc server port=%d...\n", DEFAULT_RPC_PORT);
    printf("Use 'Ctrl+C' to stop the server\n");

    rpc_context_t *ctx = rpc_init();
    if (ctx == NULL) {
      perror("error rpc_init");
      return EXIT_FAILURE;
    }

    /* Main server loop - waits for signal to shutdown */
    while (atomic_load(&server_running) && atomic_load(&ctx->keep_running)) {
      sleep(1);
    }

    /* Signal handler will set server_running to 0 */
    printf("Shutting down server...\n");

    /* Cleanup */
    rpc_deinit(ctx);
    printf("RPC server stopped\n");
  }

  return EXIT_SUCCESS;
}
