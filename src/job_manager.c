/**
 * @file job_manager.c
 * @brief Manages the creation, lifecycle, and scheduling of print jobs within the presi spooler system.
 *
 * This module holds an array of active jobs, provides functions to create jobs,
 * schedule them on compatible printers, manage conversion pipelines, and handle
 * job termination or cleanup. The design includes concurrency safeguards (a mutex)
 * for shared data and a 10-second delay for final job removal, ensuring a brief
 * window for inspection of completed or aborted jobs.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>

#include "job_manager.h"
#include "printer_manager.h"
#include "printer_struct.h"
#include "conversions.h"
#include "presi.h"

/** @brief Global array storing all tracked print jobs. */
static JOB job_spool[MAX_JOBS];

/** @brief Current count of active jobs in job_spool. */
static int job_count = 0;

/** @brief Mutex used to synchronize access to job-related data structures. */
static pthread_mutex_t job_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Launches a conversion pipeline for a print job.
 *
 * This function forks a master pipeline process, which sets a new process group
 * and launches a series of child processes — one for each conversion stage — connected
 * via pipes. If no conversion is needed (i.e., `path == NULL`), a single stage with `/bin/cat`
 * is used to directly stream the input file to the printer.
 *
 * The final stage in the pipeline writes to a file descriptor obtained by calling
 * `presi_connect_to_printer()` for the job's assigned printer.
 *
 * Each process in the pipeline becomes part of the same process group, allowing the
 * spooler to manage the entire job using signals (e.g., SIGSTOP, SIGCONT, SIGTERM).
 *
 * The master waits for all child stages to complete and exits with a status of 0
 * if all stages succeed, or 1 if any stage fails.
 *
 * @param job  Pointer to the JOB structure for which the pipeline is launched.
 * @param path A NULL-terminated array of CONVERSION pointers representing the
 *             conversion stages. If NULL, no conversion is needed.
 * @return The PID of the master pipeline process (to be stored as job->pgid), or -1 on failure.
 */
static pid_t start_conversion_pipeline(JOB *job, CONVERSION **path) {
    if (!job) return -1;

    int num_stages = 0;
    if (path) {
        while (path[num_stages]) {
            num_stages++;
        }
    }

    pid_t master = fork();
    if (master < 0) {
        return -1;
    }

    if (master == 0) {
        // Child (master of the pipeline) — will launch all stages and wait on them
        setpgid(0, 0);  // Establish a new process group for the pipeline

        int prev_fd = -1;  // Used to hold read end of the previous pipe

        for (int i = 0; i < num_stages || (num_stages == 0 && i == 0); i++) {
            int pipefd[2];
            int is_last = ((i == num_stages - 1) || (num_stages == 0));

            if (!is_last && pipe(pipefd) < 0) {
                exit(1);
            }

            pid_t stage = fork();
            if (stage < 0) {
                exit(1);
            }

            if (stage == 0) {
                // Stage child process — runs one conversion stage
                setpgid(0, 0);  // Join the pipeline's process group

                // Handle input source
                if (i == 0) {
                    // First stage: read from the input file
                    if (!job->input_file_path) exit(1);
                    int fd = open(job->input_file_path, O_RDONLY);
                    if (fd < 0) exit(1);
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                } else {
                    // Intermediate or last: read from previous pipe
                    dup2(prev_fd, STDIN_FILENO);
                    close(prev_fd);
                }

                // Handle output sink
                if (!is_last) {
                    // Intermediate stage: write to next pipe
                    close(pipefd[0]);
                    dup2(pipefd[1], STDOUT_FILENO);
                    close(pipefd[1]);
                } else {
                    // Last stage: connect to printer
                    if (!job->target_printer ||
                        !job->target_printer->name ||
                        !job->target_printer->type ||
                        !job->target_printer->type->name) {
                        exit(1);
                    }

                    int fd = presi_connect_to_printer(job->target_printer->name,
                                                      job->target_printer->type->name,
                                                      PRINTER_NORMAL);
                    if (fd < 0) exit(1);
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }

                // Determine the executable and its arguments
                char **args = NULL;
                if (num_stages == 0) {
                    // No conversion: use passthrough
                    args = (char *[]) { "/bin/cat", NULL };
                } else if (path && path[i] && path[i]->cmd_and_args) {
                    args = path[i]->cmd_and_args;
                } else {
                    exit(1);  // Invalid stage
                }

                execvp(args[0], args);  // Execute stage command
                exit(1);  // exec failed
            }

            // Parent (master): prepare for next iteration
            if (prev_fd != -1) {
                close(prev_fd);
            }

            if (!is_last) {
                prev_fd = pipefd[0];  // Save read end for next stage
                close(pipefd[1]);     // Parent closes write end
            }
        }

        // Master waits for all stages and checks for failures
        int status, failed = 0;
        while (wait(&status) > 0) {
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                failed = 1;
            }
        }

        exit(failed);
    }

    // Parent (spooler) — return PID of master for tracking
    return master;
}


/**
 * @brief Initializes the job manager, resetting job_count.
 *
 * This should be called once at startup to ensure a clean state for the job list.
 */
void job_manager_initialize(void) {
    job_count = 0;
}

/**
 * @brief Helper function to release resources allocated to a job.
 *
 * Frees the input_file_path and resets the job fields to defaults,
 * preparing the slot for reuse or permanent removal.
 *
 * @param job Pointer to the JOB structure to be cleaned.
 */
static void cleanup_job(JOB *job) {
    if (!job) return;
    free(job->input_file_path);
    job->input_file_path = NULL;
    job->target_printer = NULL;
    job->status = JOB_DELETED;
    job->pgid = -1;
    job->created_at = 0;
    job->status_changed_at = 0;
}

/**
 * @brief Cleans up all tracked jobs, releasing memory.
 *
 * Iterates through the job list and cleans every entry using cleanup_job().
 * Resets job_count to zero.
 */
void job_manager_cleanup(void) {
    for (int i = 0; i < job_count; i++) {
        cleanup_job(&job_spool[i]);
    }
    job_count = 0;
}




/**
 * @brief Submits a new print job to the spooler.
 *
 * This function registers a new job in the job spool. If a compatible printer is supplied,
 * the job is launched immediately. Otherwise, the job is marked as JOB_CREATED and
 * scheduled via try_scheduling_jobs().
 *
 * Responsibilities:
 * - Validates the file path and job limit.
 * - Infers the file type of the print file.
 * - Verifies printer compatibility and availability if specified.
 * - Registers job metadata in the spool.
 * - Launches the job immediately or delegates to the scheduler.
 * - Logs spooler events for job lifecycle tracking.
 *
 * @param file_path The path to the input file to be printed.
 * @param printer   Pointer to a specific PRINTER if requested; NULL if the system should auto-assign.
 * @return 0 on success, or -1 on failure (invalid input, no printer, etc.)
 */
int submit_print_job(const char *file_path, PRINTER *printer) {
    // Reject invalid file path or full spool
    if (!file_path || job_count >= MAX_JOBS) {
        return -1;
    }

    // Infer file type from file extension or name
    FILE_TYPE *from_type = infer_file_type((char *)file_path);
    if (!from_type) {
        return -1;
    }

    // If a printer is provided, validate that it can accept this file type (or a conversion path exists)
    if (printer) {
        if (printer->status != PRINTER_IDLE) {
            return -1;
        }
        if (printer->type != from_type) {
            CONVERSION **path = find_conversion_path(from_type->name, printer->type->name);
            if (!path) {
                return -1;
            }
            free(path);
        }
    }

    // Initialize and store job metadata in the global spool
    pthread_mutex_lock(&job_mutex);
    JOB *job = &job_spool[job_count];
    job->id = job_count;
    job->input_file_path = strdup(file_path);
    job->target_printer = printer;
    job->pgid = -1;
    job->created_at = time(NULL);
    job->status_changed_at = job->created_at;
    pthread_mutex_unlock(&job_mutex);

    sf_job_created(job->id, job->input_file_path, from_type->name);

    // Case 1: No printer specified — defer to scheduler
    if (!printer) {
        pthread_mutex_lock(&job_mutex);
        job->status = JOB_CREATED;
        pthread_mutex_unlock(&job_mutex);
        sf_job_status(job->id, JOB_CREATED);

        job_count++;  // Important: count must be updated before scheduling
        try_scheduling_jobs();
    }
    // Case 2: Printer specified — launch immediately
    else {
        CONVERSION **path = find_conversion_path(from_type->name, printer->type->name);
        pid_t pid = start_conversion_pipeline(job, path);
        if (pid < 0) {
            if (path) free(path);
            return -1;
        }

        pthread_mutex_lock(&job_mutex);
        job->status = JOB_RUNNING;
        job->pgid = pid;
        job->status_changed_at = time(NULL);
        printer->status = PRINTER_BUSY;
        pthread_mutex_unlock(&job_mutex);

        // Format command list for logging
        char *cmds[64] = { NULL };
        if (path) {
            for (int i = 0; path[i]; i++)
                cmds[i] = path[i]->cmd_and_args[0];
            free(path);
        } else {
            cmds[0] = "cat";  // Passthrough fallback
        }

        sf_job_status(job->id, JOB_RUNNING);
        sf_printer_status(printer->name, PRINTER_BUSY);
        sf_job_started(job->id, printer->name, pid, cmds);

        job_count++;  // Must update count for directly launched jobs too
    }

    // Print summary metadata for CLI feedback
    char created_str[64], status_str[64];
    strftime(created_str, sizeof(created_str), "%d %b %H:%M:%S", localtime(&job->created_at));
    strftime(status_str, sizeof(status_str), "%d %b %H:%M:%S", localtime(&job->status_changed_at));
printf("JOB[%d]: type=%s, creation(%s), status(%s)=%s, eligible=ffffffff, file=%s%s%s\n",
       job->id, from_type->name, created_str, status_str,
       job_status_names[job->status], job->input_file_path,
       job->target_printer ? ", printer=" : "",
       job->target_printer ? job->target_printer->name : "");


    return 0;
}

/**
 * @brief Attempts to schedule jobs in the CREATED state to compatible idle printers.
 *
 * This function iterates through all jobs currently in the CREATED state. For each one,
 * it checks for an available IDLE printer that can either:
 *   - Directly handle the job's file type, or
 *   - Accept the file type via a valid conversion path
 *
 * If a compatible printer is found, the job is launched via a conversion pipeline,
 * updated to JOB_RUNNING, and the printer is marked BUSY. The job's process group ID (PGID)
 * is tracked for signal control.
 */
void try_scheduling_jobs(void) {
    for (int i = 0; i < get_job_count(); i++) {
        JOB *job = get_job_by_index(i);
        if (!job || job->status != JOB_CREATED) {
            continue; // Skip if null or not in a schedulable state
        }

        FILE_TYPE *from_type = infer_file_type(job->input_file_path);
        if (!from_type) {
            continue; // Skip if file type can't be determined
        }

        PRINTER *printer = select_compatible_printer(from_type);
        if (!printer) {
            continue; // No compatible printer available
        }

        pthread_mutex_lock(&job_mutex);
        job->target_printer = printer;

        // Case: Direct passthrough (same file type as printer)
        if (strcmp(from_type->name, printer->type->name) == 0) {
            pid_t pid = start_conversion_pipeline(job, NULL);
            if (pid < 0) {
                job->target_printer = NULL;
                pthread_mutex_unlock(&job_mutex);
                continue;
            }

            job->pgid = pid;
            job->status = JOB_RUNNING;
            job->status_changed_at = time(NULL);
            printer->status = PRINTER_BUSY;
            pthread_mutex_unlock(&job_mutex);

            char *cmds[2] = { "cat", NULL };
            sf_job_status(job->id, JOB_RUNNING);
            sf_printer_status(printer->name, PRINTER_BUSY);
            sf_job_started(job->id, printer->name, pid, cmds);
            continue;
        }

        // Case: Conversion path needed
        CONVERSION **path = find_conversion_path(from_type->name, printer->type->name);
        if (!path) {
            job->target_printer = NULL;
            pthread_mutex_unlock(&job_mutex);
            continue;
        }

        pid_t pid = start_conversion_pipeline(job, path);
        if (pid < 0) {
            job->target_printer = NULL;
            pthread_mutex_unlock(&job_mutex);
            free(path);
            continue;
        }

        job->pgid = pid;
        job->status = JOB_RUNNING;
        job->status_changed_at = time(NULL);
        printer->status = PRINTER_BUSY;
        pthread_mutex_unlock(&job_mutex);

        char *cmds[64] = { NULL };
        for (int j = 0; path[j]; j++) {
            cmds[j] = path[j]->cmd_and_args[0];
        }
        free(path);

        sf_job_status(job->id, JOB_RUNNING);
        sf_printer_status(printer->name, PRINTER_BUSY);
        sf_job_started(job->id, printer->name, pid, cmds);
    }
}



/**
 * @brief Removes jobs that have been finished or aborted for at least 10 seconds.
 *
 * Finished (JOB_FINISHED) or aborted (JOB_ABORTED) jobs remain visible for
 * 10 seconds after completion, to allow users to inspect their statuses. Once
 * 10 seconds elapse, the jobs are cleaned and removed from the spool, freeing
 * up their slots for new jobs.
 */
void delete_expired_jobs_if_needed(void) {
    time_t now = time(NULL);
    for (int i = 0; i < job_count; i++) {
        JOB *job = &job_spool[i];
        if ((job->status == JOB_FINISHED || job->status == JOB_ABORTED) &&
            difftime(now, job->status_changed_at) >= 10.0) {
            sf_job_deleted(job->id);
            cleanup_job(job);

            /* Compact the array after removing the job. */
            for (int j = i + 1; j < job_count; j++) {
                job_spool[j - 1] = job_spool[j];
            }
            job_count--;
            i--;
        }
    }
}

/**
 * @brief Retrieves a pointer to a job by its index in job_spool.
 *
 * @param index Zero-based index of the desired job.
 * @return A pointer to the JOB, or NULL if the index is invalid.
 */
JOB *get_job_by_index(int index) {
    if (index < 0 || index >= job_count) {
        return NULL;
    }
    return &job_spool[index];
}

/**
 * @brief Returns the current number of active jobs.
 *
 * @return The total count of jobs stored in job_spool.
 */
int get_job_count(void) {
    return job_count;
}

/**
 * @brief Cancels a job that may be in CREATED, RUNNING, or PAUSED state.
 *
 * If the job is RUNNING or PAUSED, its pipeline is signaled to end (SIGTERM,
 * with a SIGCONT if paused). The job and printer states are updated, and spooler
 * event functions are called to record the change.
 *
 * @param job_id Numeric ID of the job to cancel.
 * @return 0 on success, -1 if the job cannot be canceled (invalid ID or wrong state).
 */
int cancel_job(int job_id) {
    if (job_id < 0 || job_id >= job_count) {
        return -1;
    }

    JOB *job = &job_spool[job_id];

    /* If the job has not started running yet, simply mark it as aborted. */
    if (job->status == JOB_CREATED) {
        pthread_mutex_lock(&job_mutex);
        job->status = JOB_ABORTED;
        job->status_changed_at = time(NULL);
        pthread_mutex_unlock(&job_mutex);

        sf_job_status(job->id, JOB_ABORTED);
        sf_job_aborted(job->id, 0);
        return 0;
    }

    /* If the job is not in a state that can be canceled, return an error. */
    if (job->status != JOB_RUNNING && job->status != JOB_PAUSED) {
        return -1;
    }

    /* If paused, ensure the pipeline is continued so it can receive SIGTERM. */
    if (job->status == JOB_PAUSED) {
        killpg(job->pgid, SIGCONT);
    }
    killpg(job->pgid, SIGTERM);

    pthread_mutex_lock(&job_mutex);
    job->status = JOB_ABORTED;
    job->status_changed_at = time(NULL);
    job->target_printer->status = PRINTER_IDLE;
    pthread_mutex_unlock(&job_mutex);

    sf_job_status(job->id, JOB_ABORTED);
    sf_printer_status(job->target_printer->name, PRINTER_IDLE);
    sf_job_aborted(job->id, 0);
    return 0;
}

/**
 * @brief Attempts to pause a running print job by sending SIGSTOP to its process group.
 *
 * According to the assignment spec, this function only sends the signal.
 * The job’s actual status transition to JOB_PAUSED is handled asynchronously
 * by the SIGCHLD handler when the OS confirms the job was stopped.
 *
 * This ensures that state changes only happen in response to actual system events,
 * preserving accurate tracking of job lifecycle transitions.
 *
 * @param job_id The numeric ID of the job to pause.
 * @return 0 on suc cess, -1 on failure (invalid ID or wrong job state).
 */
int pause_job(int job_id) {
    if (job_id < 0 || job_id >= job_count) {
        return -1;  // Invalid job ID
    }

    JOB *job = &job_spool[job_id];
    if (job->status != JOB_RUNNING) {
        return -1;  // Can only pause a job that is actively running
    }

    // Send SIGSTOP to the job's entire pipeline (process group)
    return killpg(job->pgid, SIGSTOP);
}


/**
 * @brief Attempts to resume a paused print job by sending SIGCONT to its process group.
 *
 * As with pause, this function sends the signal but does not update job state directly.
 * The SIGCHLD handler is responsible for confirming continuation and setting
 * the status back to JOB_RUNNING.
 *
 * This model ensures job state transitions only occur in response to real OS signals.
 *
 * @param job_id The numeric ID of the job to resume.
 * @return 0 on success, -1 on failure (invalid ID or job not paused).
 */
int resume_job(int job_id) {
    if (job_id < 0 || job_id >= job_count) {
        return -1;  // Invalid job ID
    }

    JOB *job = &job_spool[job_id];
    if (job->status != JOB_PAUSED) {
        return -1;  // Can only resume a paused job
    }

    // Send SIGCONT to the job's process group to resume execution
    return killpg(job->pgid, SIGCONT);
}
