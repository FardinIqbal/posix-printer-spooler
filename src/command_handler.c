/**
 * @file command_handler.c
 * @brief Dispatches and executes CLI commands in the presi printer spooler system using a Doxygen-compatible format.
 *
 * This file defines handler functions for each recognized command. Each handler:
 *  - Validates argument counts and content
 *  - Invokes the relevant spooler subsystems for printers and jobs
 *  - Reports success or failure via user-facing messages and spooler framework functions (sf_cmd_ok, sf_cmd_error, etc.)
 *
 * The comments follow a narrative style while including Doxygen tags such as @param and @return.
 * Each function includes an explanation of the rationale behind design decisions, so that a
 * developer with minimal C background can understand the implementation details and their motivations.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "command_handler.h"
#include "presi.h"
#include "conversions.h"
#include "printer_manager.h"
#include "job_manager.h"

/**
 * @brief Prints a summary of all supported commands to an output stream.
 *
 * This function displays a quick reference covering each command keyword and
 * the expected arguments. Commands include 'help', 'quit', and spooler-related
 * commands like 'type', 'printer', etc.
 *
 * @param user_output_stream The FILE stream to which the help text is written.
 */
static void display_help_message(FILE *user_output_stream) {
    fprintf(user_output_stream,
        "Supported commands:\n"
        "  help                                - Show this help message.\n"
        "  quit                                - Exit the program.\n"
        "  type <file_type>                    - Declare a supported file type.\n"
        "  conversion <from> <to> <cmd...>     - Define a conversion between file types.\n"
        "  printer <name> <type>               - Declare a printer for a given file type.\n"
        "  enable <printer>                    - Enable a previously declared printer.\n"
        "  print <filename>                    - Submit a print job for a file.\n"
        "  cancel <job_id>                     - Cancel a running job.\n"
        "  pause <job_id>                      - Pause a running job.\n"
        "  resume <job_id>                     - Resume a paused job.\n"
        "  printers                            - List all registered printers and their status.\n"
        "  jobs                                - List all submitted jobs.\n"
    );
}

/**
 * @brief Prints the one-line summary of supported commands (demo-style).
 *
 * This is used after certain errors like undefined file types, malformed conversion,
 * or misparsed input — to match the demo binary exactly.
 *
 * @param out The output stream to write to.
 */
static void print_command_list_summary(FILE *out) {
    fprintf(out,
        "Commands are: help quit type printer conversion printers jobs print cancel disable enable pause resume\n");
}


/**
 * @brief Handles the 'type' command to declare a new file type (e.g., "pdf", "txt").
 *
 * Matches demo output exactly, including error formats and argument count messages.
 *
 * @param argv Array of command tokens (e.g., {"type", "pdf"}).
 * @param argc Number of tokens in argv.
 * @param out  Output stream for printing status messages or errors.
 */
static void handle_type_command(char **argv, int argc, FILE *out) {
    if (argc != 2) {
        fprintf(out,
            "Wrong number of args (given: %d, required: 1) for CLI command 'type'\n",
            argc - 1);
        sf_cmd_error("Invalid number of arguments for 'type'.");
        return;
    }

    FILE_TYPE *type = define_type(argv[1]);
    if (!type) {
        fprintf(out, "Command error: type (failed)\n");
        sf_cmd_error("define_type() failed.");
        return;
    }

    sf_cmd_ok();
}

/**
 * @brief Handles the 'conversion' command to define a conversion path between two file types.
 *
 * This function expects at least three arguments:
 *   - The source type (e.g., "pdf")
 *   - The target type (e.g., "txt")
 *   - The command (and any arguments) used to perform the conversion
 *
 * If there are fewer than 3 arguments, or either file type is undefined, or if
 * define_conversion fails, this function prints the exact error messages as seen in the demo binary.
 *
 * Example input:
 *     conversion pdf txt util/convert pdf txt
 *
 * On success, the conversion is registered and sf_cmd_ok() is called.
 * On any failure, a demo-style error is printed and sf_cmd_error() is called.
 *
 * @param argv Array of command tokens (["conversion", "from", "to", "cmd"...]).
 * @param argc Number of tokens in argv.
 * @param out  Output stream for printing status or error messages.
 */
static void handle_conversion_command(char **argv, int argc, FILE *out) {
    if (argc < 4) {
        fprintf(out,
            "Wrong number of args (given: %d, required: 3) for CLI command 'conversion'\n",
            argc - 1);
        sf_cmd_error("Invalid number of arguments for 'conversion'.");
        return;
    }

    const char *from_type = argv[1];
    const char *to_type = argv[2];

    // Match demo: print undeclared type before error if one is missing
    FILE_TYPE *from = find_type((char *)from_type);
    FILE_TYPE *to = find_type((char *)to_type);

    if (!from || !to) {
        if (!from) {
            fprintf(out, "Undeclared file type: %s\n", from_type);
        } else {
            fprintf(out, "Undeclared file type: %s\n", to_type);
        }

        sf_cmd_error("conversion");
        fprintf(out, "Command error: conversion (failed)\n");
        return;
    }

    // Prepare command + arguments
    int cmd_argc = argc - 3;
    char **cmd_and_args = calloc(cmd_argc + 1, sizeof(char *));
    if (!cmd_and_args) {
        sf_cmd_error("Memory allocation failed.");
        return;
    }

    for (int i = 0; i < cmd_argc; ++i) {
        cmd_and_args[i] = argv[i + 3];
    }
    cmd_and_args[cmd_argc] = NULL;

    // Attempt to define the conversion
    CONVERSION *conv = define_conversion((char *)from_type, (char *)to_type, cmd_and_args);
    free(cmd_and_args);

    if (!conv) {
        fprintf(out, "Command error: conversion (failed)\n");
        sf_cmd_error("define_conversion() failed.");
        return;
    }

    sf_cmd_ok();
}

/**
 * @brief Handles the 'printer' command to declare a new printer with a specified name and file type.
 *
 * This function verifies that exactly two arguments are provided after the "printer" keyword.
 * It then attempts to register a new printer in the system using the provided name and type.
 * The file type must have been previously declared via the 'type' command.
 *
 * If the printer is successfully added, its initial status is set to DISABLED and a status
 * line is printed to the output stream showing its ID, name, type, and current state.
 *
 * If the command is malformed or registration fails (due to type mismatch, name conflict,
 * or printer cap), a formatted error message is printed that exactly matches the demo version.
 *
 * This function is part of the CLI command dispatch system and should only be called
 * from handle_user_command() after input tokenization.
 *
 * Matches all output strings and error formats exactly as expected by the demo binary.
 *
 * @param argv An array of strings representing the command and its arguments.
 *             Example: {"printer", "Alice", "pdf"}
 * @param argc The number of elements in argv.
 * @param out  The output stream for printing messages (e.g., stdout or redirected file).
 */
static void handle_printer_command(char **argv, int argc, FILE *out) {
    if (argc != 3) {
        fprintf(out,
            "Wrong number of args (given: %d, required: 2) for CLI command 'printer'\n",
            argc - 1);
        sf_cmd_error("Invalid number of arguments for 'printer'.");
        return;
    }

    const char *name = argv[1];
    const char *type = argv[2];

    FILE_TYPE *file_type = find_type((char *)type);
    if (!file_type) {
        fprintf(out, "Unknown file type: %s\n", type);
        sf_cmd_error("printer");
        fprintf(out, "Command error: printer (failed)\n");
        return;
    }

    if (add_printer_to_system(name, type) != 0) {
        sf_cmd_error("printer");
        fprintf(out, "Command error: printer (failed)\n");
        return;
    }

    // This call must come after adding printer so index is up-to-date
    PRINTER *printer = get_printer_by_name(name);
    if (printer) {
        fprintf(out, "PRINTER: id=%d, name=%s, type=%s, status=%s\n",
                get_printer_count() - 1,
                printer->name,
                printer->type->name,
                printer_status_names[printer->status]);
    }

    sf_cmd_ok();
}



/**
 * @brief Handles the 'enable' command to transition a printer from DISABLED to IDLE status.
 *
 * This function verifies that the specified printer exists and sets its status to PRINTER_IDLE,
 * making it eligible to receive jobs. It also reports the updated printer state and triggers
 * job scheduling to see if any queued jobs can now be assigned.
 *
 * Matches the demo output exactly for both success and error cases.
 *
 * @param argv Array of command tokens (["enable", "printer_name"]).
 * @param argc Number of tokens in argv.
 * @param out  Output stream for printing status or error messages.
 */
static void handle_enable_command(char **argv, int argc, FILE *out) {
    if (argc != 2) {
        fprintf(out, "Wrong number of args (given: %d, required: 1) for CLI command 'enable'\n",
                argc - 1);
        sf_cmd_error("Invalid number of arguments for 'enable'.");
        return;
    }

    PRINTER *printer = get_printer_by_name(argv[1]);
    if (!printer) {
        sf_cmd_error("enable");
        fprintf(out, "Command error: enable (no printer)\n");
        return;
    }

    printer->status = PRINTER_IDLE;
    sf_printer_status(printer->name, printer->status);

    fprintf(out, "PRINTER: id=%d, name=%s, type=%s, status=%s\n",
            get_printer_count() - 1,
            printer->name,
            printer->type->name,
            printer_status_names[printer->status]);

    try_scheduling_jobs();

    sf_cmd_ok();
}


/**
 * @brief Handles the 'printers' command to list all registered printers in the spooler.
 *
 * This function iterates through all printers currently stored in the registry
 * and prints one status line per printer. Each line contains the printer's:
 *   - ID (0-based index)
 *   - Name (unique identifier)
 *   - File type (e.g., "pdf", "txt")
 *   - Current status ("disabled", "idle", "busy")
 *
 * The format of each output line exactly matches the demo binary:
 *
 *     PRINTER: id=0, name=Alice, type=pdf, status=idle
 *
 * After listing, it calls sf_cmd_ok() to indicate command success.
 *
 * @param out The output stream for printing printer information (typically stdout).
 */
static void handle_printers_command(FILE *out) {
    for (int i = 0; i < get_printer_count(); i++) {
        PRINTER *p = get_printer_by_index(i);
        if (p) {
            fprintf(out, "PRINTER: id=%d, name=%s, type=%s, status=%s\n",
                    i, p->name, p->type->name, printer_status_names[p->status]);
        }
    }
    sf_cmd_ok();
}


/**
 * @brief Handles the 'print' command to submit a new print job.
 *
 * This function checks for exactly one argument after "print", which is expected to be a file path.
 * It attempts to infer the file type from the filename using its extension. If inference fails—
 * either due to a missing extension or the type not being defined—the demo-style error message
 * is printed:
 *
 *     Undeclared file type: <filename>
 *     Commands are: help quit type printer conversion printers jobs print cancel disable enable pause resume
 *
 * If inference succeeds, the print job is submitted using the job manager. If the submission fails
 * (e.g., no printers, pipeline error, or memory allocation issue), a generic command error is shown.
 *
 * On success, the function calls sf_cmd_ok(). All failure paths call sf_cmd_error().
 *
 * @param argv Array of command tokens (e.g., {"print", "somefile.pdf"}).
 * @param argc Number of tokens in argv.
 * @param out  Output stream for user-facing error or status messages.
 */
static void handle_print_command(char **argv, int argc, FILE *out) {
    if (argc != 2) {
        fprintf(out,
            "Wrong number of args (given: %d, required: 1) for CLI command 'print'\n",
            argc - 1);
        sf_cmd_error("Invalid number of arguments for 'print'.");
        return;
    }

    FILE_TYPE *type = infer_file_type(argv[1]);
    if (!type) {
        // Match demo: only print the error line (no file type name or command list)
        fprintf(out, "Command error: print (file type)\n");
        sf_cmd_error("print");
        return;
    }

    PRINTER *printer = NULL;  // Let the job manager choose an appropriate printer

    if (submit_print_job(argv[1], printer) != 0) {
        fprintf(out, "Command error: print (failed)\n");
        sf_cmd_error("submit_print_job() failed.");
        return;
    }

    sf_cmd_ok();
}




/**
 * @brief Lists the status of all known jobs in the spooler.
 *
 * Each job is reported by calling sf_job_status with the job's ID and current state.
 * This function does not remove or alter jobs; it merely reports them.
 *
 * @param out Output stream for listing job states.
 */
static void handle_jobs_command(FILE *out) {
    for (int i = 0; i < get_job_count(); i++) {
        JOB *job = get_job_by_index(i);
        if (job) {
            sf_job_status(job->id, job->status);
        }
    }
    sf_cmd_ok();
}

/**
 * @brief Handles the 'cancel' command to abort a specified job.
 *
 * If the job is in JOB_RUNNING or JOB_PAUSED, the spooler sends signals to
 * end it and changes its status. If the job was still in JOB_CREATED, it
 * is simply marked as aborted.
 *
 * @param argv Array of command tokens (["cancel", "job_id"]).
 * @param argc Number of tokens in argv.
 * @param out  Output stream for printing messages or errors.
 */
static void handle_cancel_command(char **argv, int argc, FILE *out) {
    if (argc != 2) {
        fprintf(out, "Error: 'cancel' requires 1 argument: <job_id>\n");
        sf_cmd_error("Invalid arguments for 'cancel'");
        return;
    }

    int job_id = atoi(argv[1]);
    if (cancel_job(job_id) != 0) {
        fprintf(out, "Error: Failed to cancel job %d\n", job_id);
        sf_cmd_error("cancel_job() failed");
        return;
    }

    sf_cmd_ok();
}

/**
 * @brief Handles the 'pause' command to stop a job's pipeline via SIGSTOP.
 *
 * The spooler changes the state to JOB_PAUSED only after SIGCHLD confirms the process
 * group has stopped. This function merely initiates the pause by sending a signal.
 *
 * @param argv Array of command tokens (["pause", "job_id"]).
 * @param argc Number of tokens in argv.
 * @param out  Output stream for printing status or errors.
 */
static void handle_pause_command(char **argv, int argc, FILE *out) {
    if (argc != 2) {
        fprintf(out, "Error: 'pause' requires 1 argument: <job_id>\n");
        sf_cmd_error("Invalid arguments for 'pause'");
        return;
    }

    int job_id = atoi(argv[1]);
    if (pause_job(job_id) != 0) {
        fprintf(out, "Error: Failed to pause job %d\n", job_id);
        sf_cmd_error("pause_job() failed");
        return;
    }

    sf_cmd_ok();
}

/**
 * @brief Handles the 'resume' command to continue a previously paused job via SIGCONT.
 *
 * The spooler will move the job from JOB_PAUSED to JOB_RUNNING once SIGCHLD indicates
 * a continue event. This function only sends the resume signal; the status update
 * is triggered externally.
 *
 * @param argv Array of command tokens (["resume", "job_id"]).
 * @param argc Number of tokens in argv.
 * @param out  Output stream for printing status or errors.
 */
static void handle_resume_command(char **argv, int argc, FILE *out) {
    if (argc != 2) {
        fprintf(out, "Error: 'resume' requires 1 argument: <job_id>\n");
        sf_cmd_error("Invalid arguments for 'resume'");
        return;
    }

    int job_id = atoi(argv[1]);
    if (resume_job(job_id) != 0) {
        fprintf(out, "Error: Failed to resume job %d\n", job_id);
        sf_cmd_error("resume_job() failed");
        return;
    }

    sf_cmd_ok();
}

/**
 * @brief Routes a user-issued command to the appropriate handler based on argv[0].
 *
 * This function matches the exact behavior and output format of the demo version of the spooler.
 * It supports all officially recognized commands and enforces argument count validation where required.
 *
 * If an unknown command is given, it prints:
 *     Unrecognized command: <name>
 *
 * For argument mismatches, it prints:
 *     Wrong number of args (given: X, required: Y) for CLI command '<name>'
 *
 * Valid commands:
 * - help, quit
 * - type, conversion
 * - printer, enable, disable, printers
 * - print, cancel, pause, resume, jobs
 *
 * On any failure, this function ensures sf_cmd_error() is called.
 * On valid command execution, it ensures sf_cmd_ok() is called.
 *
 * @param argv Tokenized user input (e.g., {"type", "pdf"})
 * @param argc Number of tokens in argv
 * @param out  Output stream for status or error messages (usually stdout or redirected file)
 */
void handle_user_command(char **argv, int argc, FILE *out) {
    if (argc == 0 || argv == NULL)
        return;

    const char *cmd = argv[0];

    if (strcmp(cmd, "help") == 0) {
        if (argc != 1) {
            fprintf(out,
                "Wrong number of args (given: %d, required: 0) for CLI command 'help'\n",
                argc - 1);
            sf_cmd_error("Invalid number of arguments for 'help'");
        } else {
            // Match demo: print one-line summary instead of full help block
            fprintf(out,
                "Commands are: help quit type printer conversion printers jobs print cancel disable enable pause resume\n");
            sf_cmd_ok();
        }
    } else if (strcmp(cmd, "type") == 0) {
        handle_type_command(argv, argc, out);
    } else if (strcmp(cmd, "conversion") == 0) {
        handle_conversion_command(argv, argc, out);
    } else if (strcmp(cmd, "printer") == 0) {
        handle_printer_command(argv, argc, out);
    } else if (strcmp(cmd, "enable") == 0) {
        handle_enable_command(argv, argc, out);
    } else if (strcmp(cmd, "disable") == 0) {
        fprintf(out, "Command error: disable (not implemented)\n");
        sf_cmd_error("disable command not implemented");
    } else if (strcmp(cmd, "printers") == 0) {
        handle_printers_command(out);
    } else if (strcmp(cmd, "print") == 0) {
        handle_print_command(argv, argc, out);
    } else if (strcmp(cmd, "jobs") == 0) {
        handle_jobs_command(out);
    } else if (strcmp(cmd, "cancel") == 0) {
        handle_cancel_command(argv, argc, out);
    } else if (strcmp(cmd, "pause") == 0) {
        handle_pause_command(argv, argc, out);
    } else if (strcmp(cmd, "resume") == 0) {
        handle_resume_command(argv, argc, out);
    } else if (strcmp(cmd, "quit") == 0) {
        // 'quit' is handled directly in run_cli
        sf_cmd_ok();
    } else {
        // Matches demo: print unrecognized command without extra help
        fprintf(out, "Unrecognized command: %s\n", cmd);
        sf_cmd_error("Unknown command.");
    }
}

