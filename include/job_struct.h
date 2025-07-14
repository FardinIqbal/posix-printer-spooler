/**
 * @file job_struct.h
 * @brief Declares the data structure representing a print job in the presi spooler system.
 *
 * Each print job encapsulates all information needed to manage its lifecycle, including:
 *   - File path to be printed
 *   - Associated printer (if any)
 *   - Current status (CREATED, RUNNING, etc.)
 *   - Process group ID for sending control signals (pause, resume, cancel)
 *   - Timestamps for creation and status changes (used for cleanup timing)
 *
 * This structure is used throughout the spooler to track and operate on print jobs,
 * from creation and scheduling to final cleanup.
 */

#ifndef JOB_STRUCT_H
#define JOB_STRUCT_H

#include <sys/types.h>  ///< Defines pid_t for process group tracking
#include <time.h>       ///< Provides time_t for timestamps

#include "presi.h"          ///< Provides the JOB_STATUS enumeration
#include "printer_struct.h" ///< Reference to the printer structure for linking a job to a printer

/**
 * @struct job
 * @brief Represents a single print job within the spooler.
 *
 * A print job remains in the system until it finishes or is aborted, plus
 * an additional 10 seconds if the user wishes to inspect its final state.
 */
struct job
{
    /**
     * @brief A unique, system-assigned identifier for the job.
     *
     * Used in user commands like `cancel 0` and `pause 2`.
     * Must be unique among all active jobs.
     */
    int id;

    /**
     * @brief The file path that this job is printing.
     *
     * Can be absolute or relative; this is where the conversion pipeline
     * will read data from.
     */
    char* input_file_path;

    /**
     * @brief The printer selected to handle this job, if any.
     *
     * If no printer is specified at job creation, the spooler will assign
     * one automatically when an eligible printer is found.
     */
    PRINTER* target_printer;

    /**
     * @brief Current status of the job in its lifecycle.
     *
     * Defined by the JOB_STATUS enum in presi.h, which includes:
     * - JOB_CREATED
     * - JOB_RUNNING
     * - JOB_PAUSED
     * - JOB_FINISHED
     * - JOB_ABORTED
     * - JOB_DELETED
     */
    JOB_STATUS status;

    /**
     * @brief The process group ID of the conversion pipeline handling this job.
     *
     * All processes in the pipeline share this PGID, allowing group-wide signals
     * (e.g., SIGSTOP, SIGCONT, SIGTERM) to pause, resume, or cancel the pipeline.
     */
    pid_t pgid;

    /**
     * @brief The timestamp indicating when this job was created.
     *
     * Used for displaying job creation times and helps with debugging or logging.
     */
    time_t created_at;

    /**
     * @brief Timestamp of the most recent status change for this job.
     *
     * If the job is FINISHED or ABORTED, this timestamp is used to calculate
     * how long the job remains in the system before deletion.
     */
    time_t status_changed_at;
};

#endif // JOB_STRUCT_H
