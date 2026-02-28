/**
 * @file command.h
 * @brief FTP command handler registration and dispatch
 * @version 0.1
 * @date 2025-10-30
 *
 */
#ifndef COMMAND_H
#define COMMAND_H

#include "protocol.h"

/**
 * @brief Maximum number of command handlers that can be registered.
 */
#define CMD_MAX_HANDLERS 64

/**
 * @brief Opaque handle for command handler context.
 *
 * This will be passed to command handlers and contains session-specific data.
 */
typedef void *cmd_handler_context_t;

/**
 * @brief Command handler function pointer type.
 *
 * Handler functions are called when a matching command is received.
 *
 * @param context The handler context (session data).
 * @param cmd The parsed command structure.
 * @return 0 on success, -1 on error.
 */
typedef int (*cmd_handler_t)(cmd_handler_context_t context, const proto_command_t *cmd);

/**
 * @brief Initializes the command module.
 *
 * Must be called once before using any other command functions.
 *
 * @return 0 on success.
 */
int cmd_init(void);

/**
 * @brief Cleans up the command module.
 *
 * Should be called once when the application is exiting.
 */
void cmd_cleanup(void);

/**
 * @brief Registers a command handler.
 *
 * Multiple handlers can be registered. When a command is parsed,
 * first the previous handler (if any) will be called, then the matching handler will be called.
 *
 * @param command The command name (e.g., "USER", "PASS"). Case-insensitive.
 * @param handler The handler function to call for this command.
 * @param prev_handler The previous handler function to call before the handler (can be NULL).
 * @return 0 on success, -1 if the handler table is full or invalid parameters.
 */
int cmd_register_handler(const char *command, cmd_handler_t handler, cmd_handler_t prev_handler);

/**
 * @brief Unregisters a command handler.
 *
 * @param command The command name to unregister.
 * @return 0 on success, -1 if the command was not found.
 */
int cmd_unregister_handler(const char *command);

/**
 * @brief Dispatches a parsed command to its registered handler.
 *
 * Looks up the command name in the handler table and calls the
 * corresponding handler function.
 *
 * @param context The handler context to pass to the handler.
 * @param cmd The parsed command structure.
 * @return 0 on success, -1 if no handler is registered or handler failed.
 */
int cmd_dispatch(cmd_handler_context_t context, const proto_command_t *cmd);

/**
 * @brief Checks if a command has a registered handler.
 *
 * @param command The command name to check.
 * @return 1 if registered, 0 if not registered.
 */
int cmd_is_registered(const char *command);

/**
 * @brief Gets the number of registered handlers.
 *
 * @return The number of currently registered command handlers.
 */
int cmd_get_handler_count(void);

/**
 * @brief Registers all standard FTP command handlers.
 *
 * This is a convenience function that registers handlers for common
 * FTP commands (USER, PASS, QUIT, PWD, CWD, etc. in RFC 959).
 * The actual handler implementations are provided separately.
 *
 * @return 0 on success, -1 if any registration fails.
 */
int cmd_register_standard_handlers(void);

/**
 * @brief Gets a comma-separated list of all registered commands.
 *
 * @return A string containing all registered command names.
 */
const char *cmd_get_all_registered_commands(void);

#endif