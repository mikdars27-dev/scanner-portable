#ifndef DRIVERS_WATCHDOG_H
#define DRIVERS_WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Watchdog abstraction.
 *
 * Digunakan untuk memastikan bahwa state machine scanner (POWER_ON,
 * SELF_TEST, IDLE, ... OPERASI, TUTUP_SESI, SLEEP) tidak terkurung
 * selamanya. Jika watchdog tidak diberi "kick" dalam waktu yang ditentukan,
 * perangkat akan di-reset.
 *
 * Implementasi saat ini adalah interface/stub. Body fungsi perlu
 * diimplementasikan menggunakan ESP32 Timer Watchdog (TWDT) atau
 * ESP.restart() sebagai fallback.
 * ========================================================================= */

typedef enum {
    WATCHDOG_OK         = 0,
    WATCHDOG_ERR_INVALID_ARG = -1,
    WATCHDOG_ERR_INIT_FAIL   = -2,
    WATCHDOG_ERR_KICK_FAIL   = -3,
} watchdog_status_t;

/* Inisialisasi watchdog dengan timeout `timeout_ms`. Setelah diinisialisasi,
 * watchdog_kick() harus dipanggil secara berkala sebelum timeout tercapai. */
watchdog_status_t watchdog_init(uint32_t timeout_ms);

/* Memberi "kick" pada watchdog (reset waktu hitung mundur). Harus dipanggil
 * secara berkala dari loop utama. */
watchdog_status_t watchdog_kick(void);

/* Menonaktifkan watchdog (hanya untuk pemulihan / mode service). */
watchdog_status_t watchdog_disable(void);

/* Mengatur callback yang akan dipanggil sebelum reset watchdog (best effort,
 * mis. untuk menyimpan state atau menutup sesi ECU dengan aman). */
typedef void (*watchdog_callback_t)(void);
watchdog_status_t watchdog_set_reset_callback(watchdog_callback_t cb);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_WATCHDOG_H */
