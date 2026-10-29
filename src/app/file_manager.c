/*
 * app/file_manager.c — SD card file browser application layer.
 *
 * Implements the app/file_manager.h API: a directory/file browser drawn
 * on the TFT and driven by the physical buttons. SELECT enters a
 * directory or picks a file, BACK goes up one level (or cancels at the
 * start directory), and file deletion is confirmed through the warning
 * screen before sd_remove() is called.
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#include "file_manager.h"

#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui/fonts.h"   /* FONT_WIDTH, FONT_SPACING */

/* ------------------------------------------------------------------------
 * Internal handle
 * ------------------------------------------------------------------------ */
struct file_manager {
    file_manager_config_t cfg;
    bool                  initialized;
};

/* ------------------------------------------------------------------------
 * Layout constants
 * ------------------------------------------------------------------------ */

#define FM_PATH_MAX     128U   /* max path length handled internally */
#define FM_ITEM_X       10     /* left margin for entry text (pixels) */
#define FM_ITEM_Y_START 50     /* first entry row (pixels) */
#define FM_ITEM_H       24     /* row pitch including spacing (pixels) */
#define FM_VISIBLE      6      /* entries visible at once */
#define FM_TITLE_Y      20     /* title row (pixels) */
#define FM_HINT_Y       260    /* key hint row (pixels) */
#define FM_TITLE_SIZE   2      /* title font size multiplier */
#define FM_ENTRY_SIZE   1      /* entry font size multiplier */
#define FM_ENTRY_MAX    33     /* max entry chars shown; longer => truncated */

/* ------------------------------------------------------------------------
 * Path helpers
 * ------------------------------------------------------------------------ */

/* Join dir + "/" + name into buf. Returns false when it does not fit. */
static bool fm_join_path(char *buf, size_t len,
                         const char *dir, const char *name)
{
    int n = snprintf(buf, len, "%s/%s", dir, name);
    return n > 0 && (size_t)n < len;
}

/* Strip the last path component from path (parent directory).
 * Returns false when there is no parent (root / single component). */
static bool fm_parent_path(char *buf, size_t len, const char *path)
{
    size_t n = strlen(path);
    while (n > 0 && path[n - 1] == '/') {
        n--;
    }
    if (n == 0) {
        snprintf(buf, len, "/");
        return false;
    }

    const char *slash = NULL;
    for (size_t i = n; i-- > 0;) {
        if (path[i] == '/') {
            slash = path + i;
            break;
        }
    }

    if (slash == NULL) {
        snprintf(buf, len, ".");
        return false;
    }

    size_t keep = (size_t)(slash - path);
    if (keep == 0) {
        snprintf(buf, len, "/");
    } else {
        snprintf(buf, len, "%.*s", (int)keep, path);
    }
    return true;
}

/* True when path lives inside start_dir (start_dir itself included). */
static bool fm_within_start(const char *start_dir, const char *path)
{
    size_t sl = strlen(start_dir);
    if (strlen(path) < sl) {
        return false;
    }
    if (memcmp(path, start_dir, sl) != 0) {
        return false;
    }
    return path[sl] == '\0' || path[sl] == '/';
}

/* True when path resolves to a directory (sd_readdir succeeds on it). */
static bool fm_is_dir(const char *path)
{
    char    **tmp = NULL;
    size_t    tmp_count = 0;
    sd_status_t st = sd_readdir(path, &tmp, &tmp_count);
    if (tmp != NULL) {
        sd_readdir_free(tmp, tmp_count);
    }
    return st == SD_OK;
}

/* Truncate name (with "...") so it fits at FM_ENTRY_SIZE. */
static void fm_entry_text(char *buf, size_t len, const char *name)
{
    size_t n = strlen(name);
    if (n <= FM_ENTRY_MAX) {
        snprintf(buf, len, "%s", name);
        return;
    }
    size_t keep = FM_ENTRY_MAX > 3 ? FM_ENTRY_MAX - 3 : 0;
    snprintf(buf, len, "%.*s...", (int)keep, name);
}

/* ------------------------------------------------------------------------
 * Rendering
 * ------------------------------------------------------------------------ */

/* Draw one browser screen: title (current dir) + entry list + hint. */
static void fm_render(file_manager_t *fm, const char *dir,
                      char **names, size_t count,
                      size_t selected, size_t top)
{
    char line[FM_PATH_MAX];

    display_fill_screen(fm->cfg.disp, DISPLAY_COLOR_BLACK);

    /* Title: current directory, right-truncated with "..." when needed. */
    size_t max_title = DISPLAY_WIDTH /
                       ((FONT_WIDTH + FONT_SPACING) * FM_TITLE_SIZE);
    if (strlen(dir) > max_title) {
        size_t keep = max_title > 3 ? max_title - 3 : 0;
        snprintf(line, sizeof(line), "...%s", dir + strlen(dir) - keep);
        display_draw_text(fm->cfg.disp, FM_ITEM_X, FM_TITLE_Y, line,
                          DISPLAY_COLOR_CYAN, FM_TITLE_SIZE);
    } else {
        display_draw_text(fm->cfg.disp, FM_ITEM_X, FM_TITLE_Y, dir,
                          DISPLAY_COLOR_CYAN, FM_TITLE_SIZE);
    }

    /* Clamp the scroll window to the item range. */
    size_t start = top;
    size_t end = start + FM_VISIBLE;
    if (end > count) {
        end = count;
    }

    /* Entries. */
    for (size_t i = start; i < end; i++) {
        int16_t y = (int16_t)(FM_ITEM_Y_START +
                              (int16_t)(i - start) * FM_ITEM_H);
        bool    is_sel = (i == selected);

        if (is_sel) {
            display_draw_rect(fm->cfg.disp, FM_ITEM_X - 4, y - 2, 200,
                              (int16_t)(FM_ITEM_H - 4), DISPLAY_COLOR_BLUE);
        }

        char entry[FM_ENTRY_MAX + 1];
        fm_entry_text(entry, sizeof(entry), names[i]);
        display_draw_text(fm->cfg.disp, FM_ITEM_X, y, entry,
                          is_sel ? DISPLAY_COLOR_YELLOW : DISPLAY_COLOR_WHITE,
                          FM_ENTRY_SIZE);
    }

    /* Scroll indicators. */
    if (start > 0) {
        display_draw_text(fm->cfg.disp, 220, FM_ITEM_Y_START - 8,
                          "^", DISPLAY_COLOR_GRAY, 1);
    }
    if (end < count) {
        display_draw_text(fm->cfg.disp, 220,
                          (int16_t)(FM_ITEM_Y_START +
                          (int16_t)(FM_VISIBLE - 1) * FM_ITEM_H + 8),
                          "v", DISPLAY_COLOR_GRAY, 1);
    }

    /* Key hint. */
    display_draw_text(fm->cfg.disp, FM_ITEM_X, FM_HINT_Y,
                      "SELECT=Pilih  BACK=Keluar",
                      DISPLAY_COLOR_CYAN, 1);
}

/* ------------------------------------------------------------------------
 * Browse
 * ------------------------------------------------------------------------ */

file_manager_status_t file_manager_browse(file_manager_t *fm,
                                          const char *start_dir,
                                          char *out_path,
                                          size_t out_path_len,
                                          uint32_t timeout_ms)
{
    if (fm == NULL) {
        return FILE_MANAGER_ERR_INVALID_ARG;
    }
    if (!fm->initialized) {
        return FILE_MANAGER_ERR_NOT_INITIALIZED;
    }
    if (start_dir == NULL || start_dir[0] == '\0' ||
        out_path == NULL || out_path_len == 0) {
        return FILE_MANAGER_ERR_INVALID_ARG;
    }
    if (strlen(start_dir) >= FM_PATH_MAX) {
        return FILE_MANAGER_ERR_INVALID_ARG;
    }
    if (!sd_is_initialized()) {
        return FILE_MANAGER_ERR_SD;
    }

    char dir[FM_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", start_dir);

    char  **names = NULL;
    size_t  count = 0;
    size_t  selected = 0;
    size_t  top = 0;
    bool    redraw = true;

    if (sd_readdir(dir, &names, &count) != SD_OK) {
        return FILE_MANAGER_ERR_SD;
    }

    uint32_t t_last = millis();

    for (;;) {
        if (redraw) {
            fm_render(fm, dir, names, count, selected, top);
            redraw = false;
        }

        uint8_t btn = buttons_read();
        if (btn != BTN_NONE) {
            t_last = millis();
        }

        if (btn & BTN_UP) {
            buttons_wait_release();
            if (selected > 0) {
                selected--;
                if (selected < top) {
                    top = selected;
                }
                redraw = true;
            }
        } else if (btn & BTN_DOWN) {
            buttons_wait_release();
            if (count > 0 && selected < count - 1) {
                selected++;
                size_t limit = top + FM_VISIBLE;
                if (selected >= limit) {
                    top = selected - FM_VISIBLE + 1;
                }
                redraw = true;
            }
        } else if (btn & BTN_SELECT) {
            buttons_wait_release();
            if (count > 0) {
                const char *entry = names[selected];

                if (strcmp(entry, ".") != 0 &&
                    strcmp(entry, "..") != 0)
                {
                    char sub[FM_PATH_MAX];
                    if (!fm_join_path(sub, sizeof(sub), dir, entry)) {
                        sd_readdir_free(names, count);
                        return FILE_MANAGER_ERR_INVALID_ARG;
                    }

                    if (fm_is_dir(sub)) {
                        /* Enter the directory. */
                        sd_readdir_free(names, count);
                        selected = 0;
                        top = 0;
                        if (sd_readdir(sub, &names, &count) != SD_OK) {
                            return FILE_MANAGER_ERR_SD;
                        }
                        snprintf(dir, sizeof(dir), "%s", sub);
                        redraw = true;
                    } else {
                        /* Pick the file. */
                        int n = snprintf(out_path, out_path_len, "%s", sub);
                        sd_readdir_free(names, count);
                        if (n < 0 || (size_t)n >= out_path_len) {
                            return FILE_MANAGER_ERR_INVALID_ARG;
                        }
                        return FILE_MANAGER_OK;
                    }
                }
            }
        } else if (btn & BTN_BACK) {
            buttons_wait_release();

            if (strcmp(dir, start_dir) == 0) {
                sd_readdir_free(names, count);
                return FILE_MANAGER_ERR_USER_CANCEL;
            }

            char par[FM_PATH_MAX];
            if (!fm_parent_path(par, sizeof(par), dir) ||
                !fm_within_start(start_dir, par))
            {
                /* Clamp back to the start directory. */
                sd_readdir_free(names, count);
                selected = 0;
                top = 0;
                if (sd_readdir(start_dir, &names, &count) != SD_OK) {
                    return FILE_MANAGER_ERR_SD;
                }
                snprintf(dir, sizeof(dir), "%s", start_dir);
                redraw = true;
            } else {
                sd_readdir_free(names, count);
                selected = 0;
                top = 0;
                if (sd_readdir(par, &names, &count) != SD_OK) {
                    return FILE_MANAGER_ERR_SD;
                }
                snprintf(dir, sizeof(dir), "%s", par);
                redraw = true;
            }
        }

        if (timeout_ms > 0 &&
            (uint32_t)(millis() - t_last) >= timeout_ms)
        {
            sd_readdir_free(names, count);
            return FILE_MANAGER_ERR_USER_CANCEL;
        }

        delay(20);
        yield();
    }
}

/* ------------------------------------------------------------------------
 * Delete
 * ------------------------------------------------------------------------ */

file_manager_status_t file_manager_delete(file_manager_t *fm,
                                          const char *path,
                                          bool *confirmed)
{
    if (confirmed != NULL) {
        *confirmed = false;
    }

    if (fm == NULL || path == NULL || path[0] == '\0') {
        return FILE_MANAGER_ERR_INVALID_ARG;
    }
    if (!fm->initialized) {
        return FILE_MANAGER_ERR_NOT_INITIALIZED;
    }
    if (!sd_is_initialized()) {
        return FILE_MANAGER_ERR_SD;
    }

    /* Confirmation screen (SELECT = hapus, BACK = batal). */
    char msg[128];
    int n = snprintf(msg, sizeof(msg), "Hapus file?\n%s", path);
    if (n < 0) {
        return FILE_MANAGER_ERR_INVALID_ARG;
    }

    bool ok = warning_show(fm->cfg.disp, WARN_CUSTOM, msg, 0);
    if (confirmed != NULL) {
        *confirmed = ok;
    }
    if (!ok) {
        return FILE_MANAGER_OK; /* user cancellation is not an error */
    }

    sd_status_t st = sd_remove(path);
    return st == SD_OK ? FILE_MANAGER_OK : FILE_MANAGER_ERR_SD;
}
