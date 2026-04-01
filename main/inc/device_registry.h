/**
 * \file
 * \brief Device registry — per-device internet access control and NVS persistence.
 *
 * Manages a list of up to #DEVICE_REG_MAX_ENTRIES registered devices.
 * Each entry stores the device MAC address, a human-readable nickname,
 * a per-device internet time counter, and an enabled/disabled toggle.
 * A "current rider" selection links the bike sensor to one specific device:
 * credits earned during an exercise session are added to that device's counter.
 *
 * Per-device counter decrement, throughput measurement, and the sliding-window
 * traffic gate are all applied once per second inside #device_reg_tick(), which
 * must be called unconditionally from the time counter 1-second callback.
 *
 * All NVS operations use the \c esport_dev namespace.
 *
 * \date 2026-03-14
 */

#ifndef DEVICE_REGISTRY_H
#define DEVICE_REGISTRY_H

#ifdef __cplusplus
extern "C"
{
#endif

//==================================================================================================
// Includes
//==================================================================================================

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

//==================================================================================================
// Constants/Macros/Datatypes
//==================================================================================================

/** Maximum number of entries in the device registry. */
#define DEVICE_REG_MAX_ENTRIES (4U)

/** Maximum number of characters in a device nickname (excluding null terminator). */
#define DEVICE_REG_NICKNAME_MAX_LEN (15U)

/** Sentinel value meaning no current rider is selected. */
#define DEVICE_REG_NO_RIDER (0xFFU)

/** Number of seconds between periodic full NVS saves of all device counters. */
#define DEVICE_REG_SAVE_INTERVAL_S (60U)

/** Length of a MAC address in bytes. */
#define DEVICE_REG_MAC_LEN (6U)

/**
 * \brief One entry in the device registry.
 *
 * This is the NVS-persisted structure stored as a blob under key \c dev_N in
 * the \c esport_dev namespace.  Per-device traffic state is RAM-only and not
 * included here.
 */
typedef struct device_reg_entry_tag
{
    uint8_t  mac[6]; /**< Device MAC address (6 bytes, big-endian). */
    char     nickname[DEVICE_REG_NICKNAME_MAX_LEN + 1U]; /**< Null-terminated nickname string. */
    uint32_t counter_s;                                  /**< Internet time credit in seconds. */
    bool     b_enabled; /**< true when internet access is permitted. */
} device_reg_entry_t;

//==================================================================================================
// Function Prototypes
//==================================================================================================

/**
 * \brief Initialise the device registry and restore state from NVS.
 *
 * Opens the \c esport_dev namespace, reads \c dev_count, and loads each saved
 * #device_reg_entry_t blob.  Resets all per-device RAM traffic counters to
 * zero.  Must be called before any other \c device_reg_* function and before
 * #wifi_mngr_init().
 *
 * \return \c ESP_OK on success, or a non-zero \c esp_err_t on failure.
 */
esp_err_t device_reg_init(void);

/**
 * \brief Return the number of registered devices.
 *
 * \return Number of valid entries (0 to #DEVICE_REG_MAX_ENTRIES).
 */
uint8_t device_reg_count_get(void);

/**
 * \brief Add a new device to the registry.
 *
 * \param[in] p_mac       Pointer to a 6-byte MAC address array.
 * \param[in] p_nickname  Null-terminated nickname (1–#DEVICE_REG_NICKNAME_MAX_LEN characters).
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_NO_MEM when the registry is full.
 * \return \c ESP_ERR_INVALID_STATE when the MAC is already registered.
 * \return \c ESP_ERR_INVALID_ARG when \p p_mac is NULL, \p p_nickname is NULL
 *         or empty, or the nickname exceeds #DEVICE_REG_NICKNAME_MAX_LEN.
 */
esp_err_t device_reg_entry_add(const uint8_t * p_mac, const char * p_nickname);

/**
 * \brief Remove the entry at the given index and compact the array.
 *
 * \param[in] idx  Index of the entry to remove (0 to count-1).
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_INVALID_ARG when \p idx >= current count.
 */
esp_err_t device_reg_entry_remove(uint8_t idx);

/**
 * \brief Copy the entry at \p idx into \p p_out.
 *
 * \param[in]  idx    Entry index (0 to count-1).
 * \param[out] p_out  Pointer to caller-supplied #device_reg_entry_t to fill.
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_INVALID_ARG when \p idx >= current count or \p p_out is NULL.
 */
esp_err_t device_reg_entry_get(uint8_t idx, device_reg_entry_t * p_out);

/**
 * \brief Update the nickname for the entry at \p idx.
 *
 * \param[in] idx        Entry index (0 to count-1).
 * \param[in] p_nickname New null-terminated nickname (1–#DEVICE_REG_NICKNAME_MAX_LEN chars).
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_INVALID_ARG when \p idx is out of range or the nickname is invalid.
 */
esp_err_t device_reg_entry_nickname_set(uint8_t idx, const char * p_nickname);

/**
 * \brief Set the enabled flag for the entry at \p idx.
 *
 * \param[in] idx       Entry index (0 to count-1).
 * \param[in] b_enabled New enabled state.
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_INVALID_ARG when \p idx >= current count.
 */
esp_err_t device_reg_entry_enabled_set(uint8_t idx, bool b_enabled);

/**
 * \brief Set the internet time counter for the entry at \p idx and persist to NVS.
 *
 * \param[in] idx       Entry index (0 to count-1).
 * \param[in] counter_s New counter value in seconds.
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_INVALID_ARG when \p idx >= current count.
 */
esp_err_t device_reg_entry_counter_set(uint8_t idx, uint32_t counter_s);

/**
 * \brief Return the internet time counter for the entry at \p idx.
 *
 * \param[in] idx  Entry index.
 *
 * \return Counter value in seconds, or \c 0 when \p idx is out of range.
 */
uint32_t device_reg_entry_counter_get(uint8_t idx);

/**
 * \brief Search the registry for a matching MAC address.
 *
 * \param[in] p_mac  Pointer to a 6-byte MAC address to search for.
 *
 * \return Index of the matching entry (0–3), or \c -1 if not found.
 */
int8_t device_reg_mac_find(const uint8_t * p_mac);

/**
 * \brief Return whether \p p_mac is allowed to route traffic to the internet.
 *
 * Returns \c true iff the MAC is registered, \c b_enabled == true, and
 * \c counter_s > 0.  Executes an O(4) spinlock-guarded scan with no NVS
 * access; safe to call from the lwIP input path.
 *
 * \param[in] p_mac  Pointer to a 6-byte MAC address.
 *
 * \return \c true if internet access is permitted, \c false otherwise.
 */
bool device_reg_mac_internet_allowed(const uint8_t * p_mac);

/**
 * \brief Return the index of the currently selected rider.
 *
 * \return Rider index (0–3), or #DEVICE_REG_NO_RIDER when no rider is selected.
 */
uint8_t device_reg_current_rider_get(void);

/**
 * \brief Set the current rider index and persist to NVS.
 *
 * Pass #DEVICE_REG_NO_RIDER to clear the selection.
 *
 * \param[in] idx  Rider index (0 to count-1), or #DEVICE_REG_NO_RIDER.
 *
 * \return \c ESP_OK on success.
 * \return \c ESP_ERR_INVALID_ARG when \p idx is not #DEVICE_REG_NO_RIDER and >= current count.
 */
esp_err_t device_reg_current_rider_set(uint8_t idx);

/**
 * \brief Apply per-device traffic gate and decrement counters for connected devices.
 *
 * Must be called exactly once per second from the time counter tick callback.
 * For each registered device: computes throughput from accumulated byte
 * counters, applies the sliding-window traffic gate, and decrements the
 * counter if the device is connected, enabled, has credits, and the gate is
 * not paused.  Triggers immediate NVS saves for devices whose counter just
 * reached zero, and a full periodic save every #DEVICE_REG_SAVE_INTERVAL_S
 * seconds.
 *
 * \return \c ESP_OK always.
 */
esp_err_t device_reg_tick(void);

/**
 * \brief Accumulate inbound bytes for the device identified by \p p_mac.
 *
 * Called from the lwIP AP input hook on every received frame.  O(4) spinlock-
 * guarded scan with no heap allocation; safe to call from the WiFi driver
 * task.  No-op if MAC is not registered.
 *
 * \param[in] p_mac   Pointer to a 6-byte source MAC address (bytes [6..11] of the Ethernet frame).
 * \param[in] bytes   Number of bytes in the received frame.
 */
void device_reg_mac_rx_bytes_add(const uint8_t * p_mac, uint32_t bytes);

/**
 * \brief Accumulate outbound bytes for the device identified by \p p_mac.
 *
 * Called from the lwIP AP linkoutput hook on every transmitted frame.  O(4)
 * spinlock-guarded scan with no heap allocation; safe to call from the lwIP
 * core task.  No-op if MAC is not registered.
 *
 * \param[in] p_mac   Pointer to a 6-byte destination MAC address (bytes [0..5] of the Ethernet
 * frame). \param[in] bytes   Number of bytes in the transmitted frame.
 */
void device_reg_mac_tx_bytes_add(const uint8_t * p_mac, uint32_t bytes);

/**
 * \brief Return the last computed 1-second throughput for the entry at \p idx.
 *
 * \param[in] idx  Entry index (0 to count-1).
 *
 * \return Throughput in kbps from the most recent tick, or \c 0 when \p idx
 *         is out of range or no tick has completed yet.
 */
uint32_t device_reg_entry_throughput_kbps_get(uint8_t idx);

/**
 * \brief Return whether the traffic gate is currently pausing decrement for the entry at \p idx.
 *
 * \param[in] idx  Entry index (0 to count-1).
 *
 * \return \c true when the sliding-window gate is holding the decrement for this device,
 *         \c false otherwise or when \p idx is out of range.
 */
bool device_reg_entry_is_paused(uint8_t idx);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_REGISTRY_H

/*** end of file ***/
