/**
 * \file
 * \brief Dynamic Activity Registry — compile-time table of all embedded activity HTML files.
 *
 * Provides a compile-time table mapping logical activity names (filename
 * without path or extension) to embedded binary data pointers, sizes, and a
 * compression flag.  The \c dyn_act_registry.c implementation is auto-generated
 * by CMake at configure time into \c ${CMAKE_CURRENT_BINARY_DIR}/generated/;
 * it must \b not be edited manually.
 *
 * The \c b_gzip field in each entry is set by the CMake code generator:
 * \c 1 when the embedded data is a gzip-compressed blob (the default), or
 * \c 0 when the file was listed in \c DYN_ACT_NO_COMPRESS and embedded raw.
 * The HTTP handler reads \c b_gzip to decide whether to set the
 * \c Content-Encoding: \c gzip response header before sending the response.
 *
 * Adding a new mini-game: place a \c .html file in \c main/dyn_activities/,
 * run \c idf.py \c reconfigure, then rebuild and reflash.
 *
 * \date 2026-04-18
 */

#ifndef DYN_ACT_REGISTRY_H
#define DYN_ACT_REGISTRY_H

//==================================================================================================
// Includes
//==================================================================================================

#include <stddef.h>
#include <stdint.h>

//==================================================================================================
// Constants/Macros/Datatypes
//==================================================================================================

/** Maximum length of a dynamic activity logical name (excluding NUL). */
#define DYN_ACT_NAME_MAX_LEN (32U)

/**
 * \brief One entry in the dynamic activity registry.
 */
typedef struct dyn_act_entry_tag
{
    const char *    p_name; /**< Logical name (filename without path or extension). */
    const uint8_t * p_data; /**< Pointer to the first byte of the embedded data.    */
    const uint8_t * p_end;  /**< Pointer one past the last byte (size = p_end - p_data). */
    uint8_t         b_gzip; /**< Non-zero if the embedded data is gzip-compressed.  */
} dyn_act_entry_t;

//==================================================================================================
// Globals
//==================================================================================================

/** Auto-generated compile-time table of all embedded dynamic activity files. */
extern const dyn_act_entry_t g_dyn_act_registry[];

/** Number of entries in #g_dyn_act_registry. */
extern const uint8_t g_dyn_act_count;

#endif /* DYN_ACT_REGISTRY_H */

/*** end of file ***/
