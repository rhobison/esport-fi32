/**
 * \file
 * \brief Buzzer feedback module public API.
 *
 * Provides non-blocking audio feedback via an active buzzer connected to a
 * configurable GPIO pin.  Four predefined beep patterns are driven by an
 * esp_timer at a 50 ms time-base.
 *
 * \date 2026-04-04
 */

#ifndef BUZZER_H
#define BUZZER_H

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Includes
//==================================================================================================

#include <stdbool.h>

#include "esp_err.h"

//==================================================================================================
// Constants/Macros/Datatypes
//==================================================================================================

/** Duration of one beep unit in milliseconds. */
#define BUZZER_UNIT_MS (50U)

/** ON-units for the session-qualifying pattern. */
#define BUZZER_PATTERN_QUALIFYING_UNITS (5U)

/** ON-units for the session-qualified pattern. */
#define BUZZER_PATTERN_QUALIFIED_UNITS (10U)

/** ON-units per beep in the session-closed pattern. */
#define BUZZER_PATTERN_CLOSED_BEEP_UNITS (2U)

/** OFF-units (gap) between beeps in the session-closed pattern. */
#define BUZZER_PATTERN_CLOSED_GAP_UNITS (1U)

/** Number of beeps in the session-closed pattern. */
#define BUZZER_PATTERN_CLOSED_BEEP_COUNT (3U)

/** ON-units for the speed-low pattern. */
#define BUZZER_PATTERN_SPEED_LOW_UNITS (2U)

/**
 * \brief Predefined buzzer pattern identifiers.
 */
typedef enum buzzer_pattern_id_tag
{
    BUZZER_PATTERN_SESSION_QUALIFYING = 0,
    BUZZER_PATTERN_SESSION_QUALIFIED  = 1,
    BUZZER_PATTERN_SESSION_CLOSED     = 2,
    BUZZER_PATTERN_SPEED_LOW          = 3,
    BUZZER_PATTERN_PASSWORD_RESET     = 4,
} buzzer_pattern_id_t;

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Initialise the buzzer module.
 *
 * Configures the buzzer GPIO as output (LOW) and creates the pattern
 * playback timer.
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t buzzer_init(void);

/**
 * \brief Play a predefined buzzer pattern.
 *
 * If a pattern is already playing, it is immediately interrupted and
 * replaced by the new one.  If buzzer feedback is disabled via
 * \c config_mngr_buzzer_enabled_get(), this function is a no-op.
 *
 * \param[in] pattern  The pattern to play.
 */
void buzzer_pattern_play(buzzer_pattern_id_t pattern);

/**
 * \brief Stop any buzzer pattern immediately.
 *
 * Sets the GPIO LOW and halts the playback timer.
 */
void buzzer_stop(void);

/**
 * \brief Update the speed-low beep state.
 *
 * When \p b_active is \c true, plays \c BUZZER_PATTERN_SPEED_LOW (with
 * normal interrupt semantics).  When \p b_active is \c false, stops the
 * speed-low pattern if it is currently playing but does \b not interrupt
 * any other pattern.
 *
 * \param[in] b_active  \c true to play speed-low beep; \c false to stop it.
 */
void buzzer_speed_low_update(bool b_active);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_H */

/*** end of file ***/
