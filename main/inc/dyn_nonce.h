/**
 * \file
 * \brief One-time nonce store for dynamic activity credit claims.
 *
 * Each time \c GET /api/dyn is called the server generates a random 8-char
 * hex nonce per activity entry.  The nonce is included in the activity URL
 * passed to the mini-game page and must be presented in the
 * \c POST /api/activities/credit body.  \c dyn_nonce_consume() validates and
 * atomically removes the nonce; a second call with the same value returns
 * \c false, preventing replayed credit requests.
 *
 * Nonces expire automatically after \c DYN_NONCE_TTL_MIN minutes so the
 * table does not fill up if a game page is abandoned without claiming.
 */

#ifndef DYN_NONCE_H
#define DYN_NONCE_H

#include <stdbool.h>
#include <stdint.h>

/** Length of a nonce string (8 uppercase hex characters), without NUL. */
#define DYN_NONCE_STRLEN (8U)

/** Nonce lifetime in minutes. */
#define DYN_NONCE_TTL_MIN (60U)

/**
 * \brief Generate a one-time nonce for a specific device + activity pair.
 *
 * The nonce is stored in the internal table and written as an 8-char
 * uppercase hex string into \p p_out (NUL-terminated).  If the table is
 * full the oldest entry is overwritten (circular).
 *
 * \param[in]  dev_idx  Device registry index (0–3).
 * \param[in]  act_id   Activity ID.
 * \param[out] p_out    Buffer of at least \c DYN_NONCE_STRLEN + 1 bytes.
 */
void dyn_nonce_generate(uint8_t dev_idx, uint32_t act_id, char * p_out);

/**
 * \brief Validate and consume a nonce.
 *
 * Returns \c true and removes the entry when \p p_nonce matches an unexpired
 * entry whose \p dev_idx and \p act_id match.  Returns \c false for any
 * mismatch, expired entry, or already-consumed nonce.
 *
 * \param[in] p_nonce  NUL-terminated nonce string (8 uppercase hex chars).
 * \param[in] dev_idx  Device registry index that must match.
 * \param[in] act_id   Activity ID that must match.
 *
 * \return \c true if the nonce was valid and has been consumed.
 */
bool dyn_nonce_consume(const char * p_nonce, uint8_t dev_idx, uint32_t act_id);

#endif /* DYN_NONCE_H */
