/*
 * app/ecu_file.h — ECU backup / restore application layer.
 *
 * Saves ECU flash images to /ecu/backup/<ecu_id>.bin on the SD card and
 * restores them back to the ECU. Flash is read/programmed in chunks through
 * the unified protocol (proto_send) and images are stored with the SD driver
 * (drivers/sd.h). Restore is gated by the WARN_ECU_FLASH safety warning.
 *
 * Layering: APP -> ecu_file_* -> proto_* -> {j1979_*, kline_proto_*} -> drivers
 *                            -> sd_* -> Arduino SD/SPI
 */

#ifndef APP_ECU_FILE_H
#define APP_ECU_FILE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"
#include "config/config.h"
#include "drivers/sd.h"          /* sd_*, sd_status_t */
#include "protocol/protocol.h"   /* proto_send, protocol_t, proto_status_t */
#include "ui/warning_screen.h"   /* warning_show, WARN_ECU_FLASH, display_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    ECU_FILE_OK                    = 0,
    ECU_FILE_ERR_INVALID_ARG       = -1,
    ECU_FILE_ERR_NOT_INITIALIZED   = -2,
    ECU_FILE_ERR_SD                = -3,    /* SD card / filesystem failure */
    ECU_FILE_ERR_PROTOCOL          = -4,    /* underlying protocol failure */
    ECU_FILE_ERR_FILE_NOT_FOUND    = -5,    /* requested file not on SD */
    ECU_FILE_ERR_NO_BACKUP         = -6,    /* no backup for this ecu_id */
    ECU_FILE_ERR_INTEGRITY         = -7,    /* backup image failed integrity check */
} ecu_file_status_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */
/* The real struct is defined only in app/ecu_file.c. */
typedef struct ecu_file ecu_file_t;

/* =========================================================================
 * Config
 * ========================================================================= */
typedef struct {
    /* Reserved (empty by design). */
} ecu_file_config_t;

/* Default config (designated initializers). */
#define ECU_FILE_CONFIG_DEFAULT { }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the ECU file service. The SD card must already be initialized
 * (sd_init) because all backup files live on SD. cfg may point to
 * ECU_FILE_CONFIG_DEFAULT. */
ecu_file_status_t ecu_file_init(const ecu_file_config_t *cfg,
                                ecu_file_t **out);

/* Release the ECU file service handle. Safe to call with NULL. */
ecu_file_status_t ecu_file_deinit(ecu_file_t *ef);

/* =========================================================================
 * Backup / restore
 * ========================================================================= */

/*
 * Back up the ECU flash image to /ecu/backup/<ecu_id>.bin.
 *
 * The flash is read in chunks with proto_send() and the complete image is
 * written to SD with sd_save_ecu_backup(). proto must be initialized and
 * timeout_ms applies per protocol round trip. Returns ECU_FILE_ERR_SD when
 * the image is larger than ECU_FILE_MAX_IMAGE.
 */
ecu_file_status_t ecu_file_backup(ecu_file_t *ef, const char *ecu_id,
                                  protocol_t *proto, uint32_t timeout_ms);

/*
 * Restore the ECU flash image from /ecu/backup/<ecu_id>.bin.
 *
 * Safety: the backup must exist (ECU_FILE_ERR_NO_BACKUP otherwise) and the
 * WARN_ECU_FLASH warning screen is shown first; SELECT is required, otherwise
 * ECU_FILE_ERR_INVALID_ARG is returned and nothing is programmed.
 */
ecu_file_status_t ecu_file_restore(ecu_file_t *ef, const char *ecu_id,
                                   protocol_t *proto, display_t *disp,
                                   uint32_t timeout_ms);

/*
 * List ECU backups on SD (/ecu/backup/*.bin). *out_names is malloc-allocated
 * by the SD driver; the caller must free it with sd_readdir_free() when done.
 */
ecu_file_status_t ecu_file_list_backups(char ***out_names, size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* APP_ECU_FILE_H */
