/**
 * @file printer_manager.h
 * @brief Declares the functions for creating, storing, and retrieving printers in the presi spooler.
 *
 * The printer manager keeps track of all printers declared by user commands,
 * ensuring they have valid names, recognized file types, and unique entries
 * in a fixed-size registry. Each printer is stored with a status (e.g. DISABLED, IDLE, BUSY),
 * which reflects its current availability for processing jobs.
 *
 * By encapsulating printer management in this module, the spooler can easily
 * query or modify printer information without exposing underlying data structures.
 * It forms a core part of the presi system, connecting user-facing commands
 * to consistent internal state management.
 */

#ifndef PRINTER_MANAGER_H
#define PRINTER_MANAGER_H

#include "presi.h"          ///< Provides definitions like PRINTER, PRINTER_STATUS

struct file_type;
typedef struct file_type FILE_TYPE;


/**
 * @brief Initializes the printer system, clearing any existing records.
 *
 * This function should be called once when the spooler starts. It frees
 * any prior data, resets counters, and ensures a clean registry state.
 */
void printer_manager_initialize(void);

/**
 * @brief Cleans up memory allocated for printer entries.
 *
 * Each printer name is allocated via strdup, so this function frees
 * those allocations, resets states, and prepares the registry for shutdown
 * or a fresh initialization.
 */
void printer_manager_cleanup(void);

/**
 * @brief Registers a new printer by name and associated file type.
 *
 * - Validates that the registry is not at maximum capacity
 * - Ensures the printer name is unique
 * - Confirms the file type is recognized (declared via "type" command)
 * - Inserts the printer into the registry with a DISABLED status
 *
 * @param printer_name   Unique string name for the new printer
 * @param file_type_name A file type recognized by the spooler
 * @return 0 on success, or -1 if a constraint is violated (duplicate name, unknown type, etc.)
 */
int add_printer_to_system(const char* printer_name, const char* file_type_name);

/**
 * @brief Finds a printer by its name.
 *
 * Performs a linear search of the registry to locate the requested printer.
 *
 * @param printer_name The string name of the printer
 * @return Pointer to the PRINTER struct if found, or NULL if no match.
 */
PRINTER* get_printer_by_name(const char* printer_name);

/**
 * @brief Reports how many printers have been registered.
 *
 * Use this count, along with get_printer_by_index, to iterate over the registry.
 *
 * @return The total number of printers in the registry.
 */
int get_printer_count(void);

/**
 * @brief Retrieves a printer by zero-based index within the registry.
 *
 * @param index Index into the registry array (0 <= index < get_printer_count()).
 * @return Pointer to the PRINTER struct if valid, or NULL otherwise.
 */
PRINTER* get_printer_by_index(int index);

PRINTER *select_compatible_printer(FILE_TYPE *from_type);

#endif // PRINTER_MANAGER_H
