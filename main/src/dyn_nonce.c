/**
 * \file
 * \brief One-time nonce store for dynamic activity credit claims.
 *
 * \date 2026-04-18
 */

//==================================================================================================
// Includes
//==================================================================================================

#include "dyn_nonce.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

//==================================================================================================
// Internal Constants / Types
//==================================================================================================

/** Number of slots in the nonce table (circular, oldest overwritten when full). */
#define DYN_NONCE_TABLE_SIZE (16U)

/** Nonce TTL in microseconds (DYN_NONCE_TTL_MIN minutes). */
#define DYN_NONCE_TTL_US ((int64_t)(DYN_NONCE_TTL_MIN) * 60LL * 1000000LL)

typedef struct
{
    uint32_t value;     /**< Random value; 0 = empty slot.          */
    uint8_t  dev_idx;   /**< Device registry index.                 */
    uint32_t act_id;    /**< Activity ID.                           */
    int64_t  issued_us; /**< esp_timer_get_time() when generated.   */
} dyn_nonce_entry_t;

//==================================================================================================
// Module State
//==================================================================================================

static dyn_nonce_entry_t g_table[DYN_NONCE_TABLE_SIZE];
static uint8_t           g_next = 0U; /**< Circular write cursor. */
static portMUX_TYPE      g_mux  = portMUX_INITIALIZER_UNLOCKED;

//==================================================================================================
// Public Functions
//==================================================================================================

void dyn_nonce_generate(uint8_t dev_idx, uint32_t act_id, char * p_out)
{
    /* Use hardware RNG; avoid 0 (reserved for "empty"). */
    uint32_t val;
    do
    {
        val = esp_random();
    } while (0U == val);

    taskENTER_CRITICAL(&g_mux);
    g_table[g_next].value     = val;
    g_table[g_next].dev_idx   = dev_idx;
    g_table[g_next].act_id    = act_id;
    g_table[g_next].issued_us = esp_timer_get_time();
    g_next                    = (uint8_t)((g_next + 1U) % DYN_NONCE_TABLE_SIZE);
    taskEXIT_CRITICAL(&g_mux);

    (void)snprintf(p_out, DYN_NONCE_STRLEN + 1U, "%08" PRIX32, val);
}

bool dyn_nonce_consume(const char * p_nonce, uint8_t dev_idx, uint32_t act_id)
{
    if ((NULL == p_nonce) || ('\0' == p_nonce[0]))
    {
        return false;
    }

    /* Parse hex string; strtoul handles uppercase and lowercase. */
    char *   p_end;
    uint32_t val = (uint32_t)strtoul(p_nonce, &p_end, 16);
    if ((0U == val) || (p_end == p_nonce))
    {
        return false;
    }

    int64_t now = esp_timer_get_time();
    bool    ok  = false;

    taskENTER_CRITICAL(&g_mux);
    for (uint8_t i = 0U; i < DYN_NONCE_TABLE_SIZE; i++)
    {
        if (0U == g_table[i].value)
        {
            continue;
        }
        /* Expire stale entries opportunistically. */
        if ((now - g_table[i].issued_us) > DYN_NONCE_TTL_US)
        {
            g_table[i].value = 0U;
            continue;
        }
        if ((g_table[i].value == val) && (g_table[i].dev_idx == dev_idx) &&
            (g_table[i].act_id == act_id))
        {
            g_table[i].value = 0U; /* Consume — invalidates immediately. */
            ok               = true;
            break;
        }
    }
    taskEXIT_CRITICAL(&g_mux);

    return ok;
}

/*** end of file ***/
