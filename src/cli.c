/**
 * @file cli.c
 * @brief Implements the core input-processing loop for the presi print spooler system in a Doxygen-compatible format.
 *
 * This module supports both interactive and batch modes for reading and interpreting user commands,
 * handling signals (SIGCHLD), and updating the states of print jobs accordingly. Each major section
 * features explanatory comments that avoid personal pronouns and highlight both functionality and rationale.
 */

#include <stdio.h>      ///< Provides FILE, stdin, stdout, fgets, etc.
#include <stdlib.h>     ///< Provides malloc, free, exit, etc.
#include <string.h>     ///< Provides strcmp, strdup, etc.
#include <ctype.h>      ///< Provides isspace
#include <signal.h>     ///< Provides signal, sig_atomic_t
#include <sys/wait.h>   ///< Provides waitpid, WIFEXITED macros
#include <unistd.h>     ///< Provides pid_t, fork, getpid
#include <time.h>       ///< Provides time, time_t, localtime

#include "sf_readline.h"
#include "command_handler.h"
#include "presi.h"
#include "printer_manager.h"
#include "job_manager.h"
#include "job_struct.h"

#define MAX_COMMAND_TOKENS 32  ///< Maximum number of tokens allowed per command line

/**
 * @var child_signal_received
 * A global flag used to indicate that SIGCHLD has been caught, signaling
 * that one or more child processes have changed state. Declared volatile
 * to prevent compiler reordering and of type sig_atomic_t to allow
 * asynchronous updates by the signal handler.
 */
static volatile sig_atomic_t child_signal_received = 0;

/**
 * @brief Signal handler for SIGCHLD.
 *
 * This function sets a global flag rather than performing complex operations.
 * This approach avoids reentrancy issues and preserves async-signal-safety.
 *
 * @param signum The signal number passed by the system (unused in this handler).
 */
static void sigchld_handler(int signum)
{
    (void)signum;
    child_signal_received = 1;
}

/**
 * @brief Processes any pending child status changes if the signal flag is set.
 *
 * This function checks if child_signal_received is nonzero and, if so, it calls
 * waitpid in a loop to handle each changed child process. The spooler’s job
 * status and printer status are updated accordingly.
 *
 * The design ensures that the signal handler itself remains minimal, and that
 * the waitpid logic, which is not guaranteed async-signal-safe, is executed in
 * a controlled environment.
 */
static void handle_child_status_updates(void)
{
    if (!child_signal_received)
    {
        return;
    }

    child_signal_received = 0; // Resets the flag for future SIGCHLD events

    int status;
    pid_t pid;

    /**
     * Calls waitpid with WNOHANG (non-blocking), WUNTRACED (detecting stops),
     * and WCONTINUED (detecting continues). Continues until no more changed
     * child processes exist.
     */
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0)
    {
        for (int i = 0; i < get_job_count(); i++)
        {
            JOB* job = get_job_by_index(i);
            if (!job || job->pgid != pid)
            {
                continue;
            }

            if (WIFSTOPPED(status))
            {
                job->status = JOB_PAUSED;
                sf_job_status(job->id, JOB_PAUSED);
            }
            else if (WIFCONTINUED(status))
            {
                job->status = JOB_RUNNING;
                sf_job_status(job->id, JOB_RUNNING);
            }
            else if (WIFEXITED(status))
            {
                job->status = JOB_FINISHED;
                job->status_changed_at = time(NULL);
                sf_job_status(job->id, JOB_FINISHED);
                sf_job_finished(job->id, WEXITSTATUS(status));
                if (job->target_printer)
                {
                    job->target_printer->status = PRINTER_IDLE;
                    sf_printer_status(job->target_printer->name, PRINTER_IDLE);
                }
            }
            else if (WIFSIGNALED(status))
            {
                job->status = JOB_ABORTED;
                job->status_changed_at = time(NULL);
                sf_job_status(job->id, JOB_ABORTED);
                sf_job_aborted(job->id, WTERMSIG(status));
                if (job->target_printer)
                {
                    job->target_printer->status = PRINTER_IDLE;
                    sf_printer_status(job->target_printer->name, PRINTER_IDLE);
                }
            }
        }
    }

    try_scheduling_jobs(); // Attempt to start eligible jobs after changes
}

/**
 * @brief Splits a line of text into an array of tokens separated by whitespace.
 *
 * The function continues until it either reaches the end of the string or fills
 * the available token slots. Leading whitespace is skipped, and additional
 * whitespace between tokens is replaced with null terminators.
 *
 * @param line   A mutable string that will be tokenized in-place.
 * @param tokens An array to hold the resulting tokens.
 * @return The number of tokens extracted from the line.
 */
static int split_input_line_into_tokens(char* line, char** tokens)
{
    int count = 0;

    while (*line != '\0')
    {
        while (isspace((unsigned char)*line))
        {
            line++;
        }
        if (*line == '\0')
        {
            break;
        }

        tokens[count++] = line;

        while (*line && !isspace((unsigned char)*line))
        {
            line++;
        }
        if (*line)
        {
            *line++ = '\0';
        }

        if (count >= MAX_COMMAND_TOKENS)
        {
            break;
        }
    }
    return count;
}

/**
 * @brief Main command-line interface loop.
 *
 * This function is called once or more by the main program to read commands
 * from either standard input or a batch file. It initializes the spooler
 * subsystems only once, installs the SIGCHLD handler, and calls handle_child_status_updates
 * before blocking for user input to safely handle job state changes.
 *
 * @param in  The input stream (stdin for interactive mode, or a file for batch mode).
 * @param out The output stream (stdout or another file).
 * @return -1 if the user typed 'quit' or EOF from stdin, or 0 if EOF is reached in batch mode.
 */
int run_cli(FILE *in, FILE *out) {
    static int initialized = 0;

    /*
     * One-time initialization for printer and job systems,
     * along with SIGCHLD setup and async-safe input handling.
     */
    if (!initialized) {
        printer_manager_initialize();
        job_manager_initialize();

        signal(SIGCHLD, sigchld_handler);
        sf_set_readline_signal_hook(handle_child_status_updates);

        initialized = 1;
    }

    while (1) {
        char *input_line = NULL;

        if (in != stdin) {
            // Batch mode: read one line of input from file
            char buffer[1024];
            if (!fgets(buffer, sizeof(buffer), in)) {
                return 0;
            }
            buffer[strcspn(buffer, "\n")] = '\0';
            input_line = strdup(buffer);
        } else {
            // Interactive mode: show prompt and read with sf_readline
            input_line = sf_readline("presi> ");
            if (input_line == NULL) {
                return -1;
            }
        }

        /*
         * Ignore lines that are blank or contain only whitespace.
         * This also ensures that lines like "   help" are not treated as valid.
         * The demo does not print any message for these.
         */
        int is_all_whitespace = 1;
        for (char *p = input_line; *p != '\0'; ++p) {
            if (!isspace((unsigned char)*p)) {
                is_all_whitespace = 0;
                break;
            }
        }

        if (is_all_whitespace || isspace((unsigned char)input_line[0])) {
            free(input_line);
            continue;
        }

        // Split the line into tokens for parsing
        char *tokens[MAX_COMMAND_TOKENS];
        int num_tokens = split_input_line_into_tokens(input_line, tokens);

        // If tokenization failed or first token is null, reject
        if (num_tokens == 0 || tokens[0] == NULL) {
            fprintf(out, "Unrecognized command: \n");
            sf_cmd_error("Unrecognized command.");
            free(input_line);
            continue;
        }

        /*
         * Handle 'quit' separately to allow argument count validation
         * and proper termination behavior.
         */
        if (strcmp(tokens[0], "quit") == 0) {
            if (num_tokens != 1) {
                fprintf(out,
                        "Wrong number of args (given: %d, required: 0) for CLI command 'quit'\n",
                        num_tokens - 1);
                sf_cmd_error("Invalid number of arguments for 'quit'");
            } else {
                sf_cmd_ok();
                free(input_line);
                return -1;
            }
        } else {
            // Dispatch normal user commands to the command handler
            handle_user_command(tokens, num_tokens, out);
        }

        delete_expired_jobs_if_needed(); // Clean up expired jobs
        free(input_line);
    }

    return 0;
}


