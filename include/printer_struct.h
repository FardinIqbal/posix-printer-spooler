/**
 * @file printer_struct.h
 * @brief Declares the data structure for representing a printer in the presi spooler.
 *
 * A printer is uniquely identified by its name and is capable of handling exactly one file type,
 * which might be extended or converted in order to match the printer’s supported type. The presi
 * system relies on this structure to track whether a printer is disabled, idle, or busy. Each
 * declared printer is managed by the printer_manager module, which coordinates creation, lookup,
 * and state transitions.
 */

#ifndef PRINTER_STRUCT_H
#define PRINTER_STRUCT_H

#include "presi.h"  ///< Provides the PRINTER_STATUS enum (DISABLED, IDLE, BUSY)

/* Forward declaration of FILE_TYPE (from conversions.h) to avoid full module inclusion here. */
struct file_type;
typedef struct file_type FILE_TYPE;

/**
 * @struct printer
 * @brief Represents a single logical printer in the presi spooler.
 *
 * This structure contains all relevant details for a printer:
 *   - A user-facing name
 *   - The single file type it supports
 *   - The printer’s availability or operational state
 *   - An optional extension field (other) for future enhancements
 */
struct printer
{
    /**
     * @brief A unique, human-readable name for the printer.
     *
     * Names must be distinct among all printers. Commands like
     * `enable alice` or `print file.pdf alice` reference this name.
     */
    char* name;

    /**
     * @brief File type supported by the printer.
     *
     * This is a pointer to a previously defined FILE_TYPE (e.g., PDF, TXT).
     * Jobs must match or convert to this type before being sent to the printer.
     */
    FILE_TYPE* type;

    /**
     * @brief The current operational status of the printer.
     *
     * Possible values:
     *  - PRINTER_DISABLED
     *  - PRINTER_IDLE
     *  - PRINTER_BUSY
     */
    PRINTER_STATUS status;

    /**
     * @brief Reserved field for future extensions (e.g., logging, queues, metrics).
     *
     * This pointer can hold auxiliary data without altering the core struct definition.
     */
    void* other;
};

#endif // PRINTER_STRUCT_H
