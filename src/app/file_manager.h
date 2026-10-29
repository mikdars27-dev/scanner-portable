/*
 * app/file_manager.h — SD card file browser application layer.
 *
 * Lets the user browse directories and files on the SD card and pick a
 * path (file_manager_browse) or delete a file (file_manager_delete).
 * The listing is drawn on the TFT (ui/display.h) and driven by the
 * physical buttons (ui/buttons.h): SELECT enters a directory or picks a
 * file, BACK goes up one level (or cancels at the start directory).
 * Deletion is confirmed through ui/warning_screen.h before sd_remove().
 *
 * Layering: APP -> file_manager_* -> {sd_*, display_*, buttons_*,
 * warning_screen_*} -> drivers + UI
 */

#ifndef APP_FILE_MANAGER_H
#define APP_FILE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config/pinout.h"
#include "config/config.h"
#include "drivers/sd.h"          /* sd_*, sd_status_t */
#include "ui/display.h"          /* display_t, DISPLAY_WIDTH/HEIGHT, colors */
#include "ui/buttons.h"          /* btn_code_t, buttons_* */
#include "ui/warning_screen.h"   /* warning_show, WARN_CUSTOM */

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Error codes
 * ========================================================================= */
typedef enum {
    FILE_MANAGER_OK                    = 0,
    FILE_MANAGER_ERR_INVALID_ARG       = -1,
    FILE_MANAGER_ERR_NOT_INITIALIZED   = -2,
    FILE_MANAGER_ERR_SD                = -3,    /* SD card / filesystem failure */
    FILE_MANAGER_ERR_USER_CANCEL       = -4,    /* user canceled (BACK / timeout) */
} file_manager_status_t;

/* =========================================================================
 * Opaque handle
 * ========================================================================= */
/* The real struct is defined only in app/file_manager.c. */
typedef struct file_manager file_manager_t;

/* =========================================================================
 * Config
 * ========================================================================= */
typedef struct {
    display_t *disp;   /* already-initialized display handle */
} file_manager_config_t;

/* Default config (designated initializers). */
#define FILE_MANAGER_CONFIG_DEFAULT(d) { .disp = (d) }

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Initialize the file manager. The SD card (sd_init) and the display
 * (display_init) must already be initialized. cfg->disp may not be NULL. */
file_manager_status_t file_manager_init(const file_manager_config_t *cfg,
                                        file_manager_t **out);

/* Release the file manager handle. Safe to call with NULL. */
file_manager_status_t file_manager_deinit(file_manager_t *fm);

/* =========================================================================
 * File browser
 * ========================================================================= */

/*
 * Browse the SD card starting at start_dir and let the user pick a file.
 *
 * The directory listing is drawn on the display. UP/DOWN move the
 * selection, SELECT enters a directory or picks a file, BACK goes up one
 * level (or cancels at the start directory).
 *
 * On success the chosen path is copied, NUL-terminated, into out_path
 * (truncated to out_path_len). timeout_ms bounds button inactivity:
 * 0 = wait forever. Returns FILE_MANAGER_ERR_USER_CANCEL when the user
 * cancels or the timeout elapses without any button press.
 */
file_manager_status_t file_manager_browse(file_manager_t *fm,
                                          const char *start_dir,
                                          char *out_path,
                                          size_t out_path_len,
                                          uint32_t timeout_ms);

/*
 * Ask the user to confirm deleting path, then remove it from the SD card.
 *
 * warning_show() (WARN_CUSTOM) is shown first: SELECT confirms, BACK
 * cancels. *confirmed is set to true only when the user confirmed. The
 * file is removed only when confirmed. Returns FILE_MANAGER_ERR_SD when
 * the removal itself fails; a user cancellation is not an error
 * (FILE_MANAGER_OK with *confirmed == false).
 */
file_manager_status_t file_manager_delete(file_manager_t *fm,
                                          const char *path,
                                          bool *confirmed);

#ifdef __cplusplus
}
#endif

#endif /* APP_FILE_MANAGER_H */
