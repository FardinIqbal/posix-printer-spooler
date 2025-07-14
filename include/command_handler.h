/**
 * @file command_handler.h
 * @brief Defines the interface for interpreting and dispatching CLI commands in the presi spooler system.
 *
 * This header declares the primary function for handling a single user command. The command
 * handler is decoupled from the lower-level CLI loop so that other parts of the application
 * (such as job or printer logic) remain independent of parsing and dispatch details.
 *
 * The commands themselves are supplied as an array of tokens, where the first token indicates
 * the command type (e.g., "type", "printer", "quit"), and the subsequent tokens are its arguments.
 * The function determines which command-specific logic to invoke and reports success or errors
 * by writing to a provided output stream.
 *
 * ### Command Coverage
 * - **Miscellaneous**: help, quit
 * - **Type/Conversion**: type, conversion
 * - **Printer Management**: printer, enable, disable, printers
 * - **Job Management**: print, cancel, pause, resume, jobs
 *
 * The design ensures that the rest of the program can focus on job scheduling, printer management,
 * and signal processing without worrying about user interaction parsing.
 */

#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <stdio.h>  ///< For FILE*

/**
 * @brief Dispatches a tokenized user command to the appropriate handler in the presi spooler.
 *
 * This function is typically invoked from the main CLI loop after a user input line has
 * been split into tokens. The first token represents the command keyword, and subsequent
 * tokens represent arguments for that command. The function identifies which command is
 * being requested and calls the corresponding logic (e.g., add a printer, submit a print
 * job, pause a job).
 *
 * @param argument_vector     Array of C strings (tokens) representing the command and its arguments.
 * @param argument_count      Number of tokens in the argument_vector.
 * @param user_output_stream  Output stream for user-facing messages (errors, confirmations, etc.).
 */
void handle_user_command(char **argument_vector, int argument_count, FILE *user_output_stream);

#endif // COMMAND_HANDLER_H
