/**
 * @file job_manager.h
 * @brief Declares the interface for creating, scheduling, and managing print jobs within the presi spooler.
 *
 * This module coordinates all aspects of job lifecycle:
 *   - Creation of a job with a specified file path and optional printer
 *   - Determination of compatible printers and invocation of conversion pipelines
 *   - Handling of user commands to pause, resume, or cancel a job
 *   - Automatic removal of jobs that have been finished or aborted for more than 10 seconds
 *
 * Internally, a fixed-size array stores active jobs, each associated with a unique ID.
 * Functions in this module ensure concurrency control, status transitions, and
 * event reporting through sf_event calls. Other components (CLI, printer_manager) rely on
 * this module to handle job-related logic cleanly and consistently.
 */

#ifndef JOB_MANAGER_H
#define JOB_MANAGER_H

#include "printer_struct.h"
#include "job_struct.h"
#include "presi.h"  // For MAX_JOBS definition

/**
 * @brief Initializes the job manager by resetting counters and clearing the job array.
 *
 * Must be called once at the beginning of program execution (e.g., in run_cli()).
 * Ensures there are no leftover jobs or references from a previous run.
 */
void job_manager_initialize(void);

/**
 * @brief Cleans up any resources allocated for jobs, including file paths.
 *
 * Calls cleanup logic on every job in the array. Useful when the program is
 * shutting down, to avoid memory leaks.
 */
void job_manager_cleanup(void);

/**
 * @brief Submits a new job to the spooler, optionally binding it to a specific printer.
 *
 * - Infers the file type based on the file name extension
 * - If no printer is given, job remains in JOB_CREATED until an appropriate printer
 *   becomes available
 * - If a printer is specified and is idle, launches the conversion pipeline immediately
 *
 * @param file_path       The path of the file to be printed (non-null).
 * @param assigned_printer A pointer to a PRINTER to use, or NULL to auto-select.
 * @return 0 on success, -1 if submission failed (e.g., no printers available or invalid file).
 */
int submit_print_job(const char* file_path, PRINTER* assigned_printer);

/**
 * @brief Tries to start any pending jobs that are in JOB_CREATED state.
 *
 * Triggered when:
 * - A new printer becomes idle
 * - A job completes, freeing its printer
 *
 * Scans the array of jobs, and for each JOB_CREATED job, checks if a printer
 * can handle its file type (directly or via conversion). If found, starts the
 * conversion pipeline, marking the job as JOB_RUNNING.
 */
void try_scheduling_jobs(void);

/**
 * @brief Retrieves a job by its 0-based index in the internal job array.
 *
 * @param index The job index to look up.
 * @return A pointer to the job at the specified index, or NULL if invalid.
 */
JOB* get_job_by_index(int index);

/**
 * @brief Reports how many jobs are currently tracked (in any state).
 *
 * @return The total number of jobs in the system.
 */
int get_job_count(void);

/**
 * @brief Deletes jobs that have stayed in FINISHED or ABORTED state for more than 10 seconds.
 *
 * Called periodically (e.g., after each user command). Ensures the system cleans up jobs
 * that no longer need to be tracked, preventing accumulation of stale data.
 */
void delete_expired_jobs_if_needed(void);

/**
 * @brief Cancels an active or created job, stopping its pipeline if necessary.
 *
 * - If the job is paused (JOB_PAUSED), a SIGCONT is sent so it can respond to SIGTERM
 * - Job status changes to JOB_ABORTED, and the printer is set to IDLE
 *
 * @param job_id Unique ID of the job to cancel.
 * @return 0 on success, -1 if the job cannot be canceled (e.g., invalid ID, wrong state).
 */
int cancel_job(int job_id);

/**
 * @brief Pauses a running job by sending SIGSTOP to its pipeline process group.
 *
 * The job eventually transitions to JOB_PAUSED once a SIGCHLD acknowledges the stop.
 *
 * @param job_id The ID of the job to pause.
 * @return 0 on success, -1 if the job is not currently running or the ID is invalid.
 */
int pause_job(int job_id);

/**
 * @brief Resumes a paused job by sending SIGCONT to its pipeline process group.
 *
 * The job transitions to JOB_RUNNING after SIGCHLD indicates it has continued.
 *
 * @param job_id The ID of the job to resume.
 * @return 0 on success, -1 if the job is not paused or the ID is invalid.
 */
int resume_job(int job_id);

#endif // JOB_MANAGER_H
