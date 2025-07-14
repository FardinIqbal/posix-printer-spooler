/**
 * @file printer_manager.c
 * @brief Manages the registry of printers in the presi print spooler.
 *
 * This module provides functions for declaring, enabling, and retrieving
 * printers. It maintains a global registry of printer structures, each associated
 * with a unique name and a file type. This abstraction centralizes printer
 * operations, simplifies job scheduling, and cleanly separates the details of
 * printer tracking from higher-level command and job management.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "printer_manager.h"
#include "printer_struct.h"
#include "conversions.h"

/** @brief A global, fixed-size array that stores every declared printer. */
static PRINTER printer_registry[MAX_PRINTERS];

/** @brief The current count of printers recorded in printer_registry. */
static int number_of_registered_printers = 0;

/**
 * @brief Initializes the internal printer registry to a clean state.
 *
 * The function clears all existing printer entries and resets the printer
 * counter to zero, ensuring no stale data remains from previous runs.
 * This should be called once when the spooler starts.
 */
void printer_manager_initialize(void) {
    printer_manager_cleanup();
    number_of_registered_printers = 0;
}

/**
 * @brief Cleans up all printer data in the registry.
 *
 * Each printer's dynamically allocated name is freed, and its fields
 * are reset to default values. This function helps prevent memory leaks
 * on shutdown or re-initialization.
 */
void printer_manager_cleanup(void) {
    for (int i = 0; i < number_of_registered_printers; ++i) {
        free(printer_registry[i].name);
        printer_registry[i].name = NULL;
        printer_registry[i].type = NULL;
        printer_registry[i].status = PRINTER_DISABLED;
        printer_registry[i].other = NULL;
    }
    number_of_registered_printers = 0;
}

/**
 * @brief Declares a new printer using the specified name and file type.
 *
 * This function verifies that:
 *   - The registry has not reached its maximum capacity.
 *   - The requested printer name is unique.
 *   - The file type is recognized by the spooler (previously defined).
 *
 * A newly added printer is assigned a DISABLED status and must be enabled
 * before it can process jobs. Upon success, sf_printer_defined is called
 * for logging or automated testing.
 *
 * @param printer_name    The unique identifier for the printer (cannot be NULL).
 * @param file_type_name  The name of the file type the printer supports (must already be declared).
 * @return 0 on success, or -1 if the operation fails (due to name conflict, unrecognized type, or capacity limit).
 */
int add_printer_to_system(const char *printer_name, const char *file_type_name) {
    if (number_of_registered_printers >= MAX_PRINTERS) {
        return -1;
    }

    if (get_printer_by_name(printer_name) != NULL) {
        return -1;  // A printer with this name already exists
    }

    FILE_TYPE *resolved_file_type = find_type((char *)file_type_name);
    if (resolved_file_type == NULL) {
        return -1;  // Unknown file type
    }

    PRINTER *new_printer = &printer_registry[number_of_registered_printers];
    new_printer->name = strdup(printer_name);
    new_printer->type = resolved_file_type;
    new_printer->status = PRINTER_DISABLED;
    new_printer->other = NULL;

    number_of_registered_printers++;

    // Notifies the spooler framework that a new printer was defined
    sf_printer_defined(new_printer->name, new_printer->type->name);
    return 0;
}

/**
 * @brief Retrieves the current count of defined printers in the registry.
 *
 * Useful when iterating over printers to find idle ones or to display printer status.
 *
 * @return The number of printers currently registered in the spooler.
 */
int get_printer_count(void) {
    return number_of_registered_printers;
}

/**
 * @brief Finds a printer by its unique name.
 *
 * The function performs a linear search over the registry to locate a matching printer.
 * If found, a pointer to that printer's struct is returned; otherwise, NULL is returned.
 *
 * @param printer_name The string name of the printer to retrieve.
 * @return A pointer to the PRINTER struct if found, or NULL if no match is found.
 */
PRINTER *get_printer_by_name(const char *printer_name) {
    for (int i = 0; i < number_of_registered_printers; i++) {
        if (strcmp(printer_registry[i].name, printer_name) == 0) {
            return &printer_registry[i];
        }
    }
    return NULL;
}

/**
 * @brief Retrieves a printer by its zero-based index in the registry.
 *
 * This function allows external code to safely iterate over all printers without
 * accessing the internal array directly. If the index is out of range, NULL is returned.
 *
 * @param index The zero-based index for the desired printer.
 * @return A pointer to the PRINTER struct, or NULL if index is invalid.
 */
PRINTER *get_printer_by_index(int index) {
    if (index < 0 || index >= number_of_registered_printers) {
        return NULL;
    }
    return &printer_registry[index];
}


/**
 * @brief Selects a compatible IDLE printer for a given input file type.
 *
 * This function is responsible for finding a printer that is currently available
 * (i.e., has status PRINTER_IDLE) and is capable of printing the specified file type.
 *
 * A printer is considered compatible with the file type if:
 *   - It supports the file type natively (i.e., the printer's type exactly matches the input type), OR
 *   - There exists a valid conversion path from the input file type to the printer's supported type.
 *
 * If a printer supports the file type directly, it is preferred over one that requires conversion.
 * As soon as the function finds such a printer, it returns it immediately.
 *
 * NOTE:
 *   - Matching is done using string comparison of file type names, not pointer equality,
 *     since different FILE_TYPE instances may exist for the same type name.
 *   - Only the first matching compatible IDLE printer is returned.
 *   - If no suitable printer is found, the function returns NULL.
 *
 * @param from_type A pointer to the FILE_TYPE struct representing the type of the input file.
 * @return A pointer to a compatible PRINTER in PRINTER_IDLE state, or NULL if none available.
 */
PRINTER *select_compatible_printer(FILE_TYPE *from_type) {
    // Reject null input — can't match if file type is unknown
    if (!from_type) {
        return NULL;
    }

    // Iterate through all registered printers
    for (int i = 0; i < get_printer_count(); i++) {
        PRINTER *printer = get_printer_by_index(i);

        // Skip invalid or uninitialized printer entries
        if (!printer) {
            continue;
        }

        // Skip printers that are currently not available
        if (printer->status != PRINTER_IDLE) {
            continue;
        }

        // Check for direct support (no conversion needed)
        if (strcmp(printer->type->name, from_type->name) == 0) {
            // The printer supports the file type natively
            return printer;
        }

        // Check for a valid conversion path from input type to printer type
        CONVERSION **path = find_conversion_path(from_type->name, printer->type->name);
        if (path) {
            // Free the temporary conversion path array
            free(path);
            // Return the first printer that can handle the type via conversion
            return printer;
        }

        // Otherwise, continue checking the next printer
    }

    // No compatible printer found
    return NULL;
}
