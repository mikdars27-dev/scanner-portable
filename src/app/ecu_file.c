/*
 * app/ecu_file.c — ECU backup / restore application layer.
 *
 * Implements the app/ecu_file.h API: flash images are read/programmed in
 * chunks via proto_send() and stored on SD under /ecu/backup/<ecu_id>.bin
 * using drivers/sd.h. Restore is gated by the WARN_ECU_FLASH safety warning.
 *
 * Toolchain note: C++ (Arduino). Compile as C++.
 */

#include "ecu_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/crc.h"   /* crc32() for backup integrity verification */

/* ------------------------------------------------------------------------
 * Internal handle
 * ------------------------------------------------------------------------ */
struct ecu_file {
    ecu_file_config_t cfg;
    bool              initialized;
};

/* ------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------ */

/* Flash-image services addressed through proto_send(). The payload format
 * ([addr:4][len:2][data..]) and the SIDs are ECU-specific; these are the
 * DIY scanner defaults. proto_send() expects [sid][data..] and answers
 * [sid+0x40][data..]. */
#define ECU_FILE_SID_READ_FLASH  0x34U
#define ECU_FILE_SID_WRITE_FLASH 0x36U

#define ECU_FILE_CHUNK_SIZE      256U   /* bytes per proto_send round trip */
#define ECU_FILE_MAX_IMAGE       (64UL * 1024UL) /* largest image handled */
#define ECU_FILE_PATH_LEN        128U   /* path buffer size */
#define ECU_FILE_BACKUP_EXT      ".bin"

/* Working image buffer. This module does not malloc/free image memory (heap
 * is bounded on ESP32), so one static buffer is shared between backup() and
 * restore(). Calls are not reentrant, which matches the single ECU focus of
 * this scanner. */
static uint8_t ecu_file_image[ECU_FILE_MAX_IMAGE];

/* ------------------------------------------------------------------------
 * Status mapping
 * ------------------------------------------------------------------------ */

static ecu_file_status_t ecu_file_from_proto(proto_status_t st)
{
    switch (st) {
        case PROTO_OK:                    return ECU_FILE_OK;
        case PROTO_ERR_INVALID_ARG:       return ECU_FILE_ERR_INVALID_ARG;
        case PROTO_ERR_NOT_INITIALIZED:   return ECU_FILE_ERR_NOT_INITIALIZED;
        case PROTO_ERR_TIMEOUT:           return ECU_FILE_ERR_PROTOCOL;
        case PROTO_ERR_PROTOCOL:          return ECU_FILE_ERR_PROTOCOL;
        case PROTO_ERR_BUFFER_TOO_SMALL:  return ECU_FILE_ERR_PROTOCOL;
        case PROTO_ERR_NEGATIVE_RESPONSE: return ECU_FILE_ERR_PROTOCOL;
        default:                          return ECU_FILE_ERR_PROTOCOL;
    }
}

static ecu_file_status_t ecu_file_from_sd(sd_status_t st)
{
    return st == SD_OK ? ECU_FILE_OK : ECU_FILE_ERR_SD;
}

/* ------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

/* Argument / state validation shared by backup() and restore(). */
static ecu_file_status_t ecu_file_check(ecu_file_t *ef, const char *ecu_id,
                                        protocol_t *proto)
{
    if (ef == NULL) {
        return ECU_FILE_ERR_INVALID_ARG;
    }
    if (!ef->initialized) {
        return ECU_FILE_ERR_NOT_INITIALIZED;
    }
    if (ecu_id == NULL || ecu_id[0] == '\0') {
        return ECU_FILE_ERR_INVALID_ARG;
    }
    if (proto == NULL) {
        return ECU_FILE_ERR_INVALID_ARG;
    }
    if (!sd_is_initialized()) {
        return ECU_FILE_ERR_SD;
    }
    return ECU_FILE_OK;
}

/* Read one flash chunk from the ECU. */
static ecu_file_status_t ecu_file_read_chunk(protocol_t *proto, uint32_t addr,
                                             uint16_t len, uint8_t *data,
                                             size_t *out_len, uint32_t timeout_ms)
{
    uint8_t req[6];
    uint8_t resp[ECU_FILE_CHUNK_SIZE];
    size_t  resp_len = 0;
    proto_status_t pst;

    req[0] = (uint8_t)(addr >> 24);
    req[1] = (uint8_t)(addr >> 16);
    req[2] = (uint8_t)(addr >> 8);
    req[3] = (uint8_t)addr;
    req[4] = (uint8_t)(len >> 8);
    req[5] = (uint8_t)len;

    pst = proto_send(proto, ECU_FILE_SID_READ_FLASH, req, sizeof(req),
                     resp, sizeof(resp), &resp_len, timeout_ms);
    if (pst != PROTO_OK) {
        return ecu_file_from_proto(pst);
    }
    if (resp_len < 1 ||
        resp[0] != (uint8_t)(ECU_FILE_SID_READ_FLASH + 0x40U))
    {
        return ECU_FILE_ERR_PROTOCOL;
    }
    *out_len = resp_len - 1;
    memcpy(data, resp + 1, *out_len);
    return ECU_FILE_OK;
}

/* Program one flash chunk on the ECU. */
static ecu_file_status_t ecu_file_write_chunk(protocol_t *proto, uint32_t addr,
                                              const uint8_t *data, uint16_t len,
                                              uint32_t timeout_ms)
{
    uint8_t req[6 + ECU_FILE_CHUNK_SIZE];
    uint8_t resp[8];
    size_t  resp_len = 0;
    proto_status_t pst;

    req[0] = (uint8_t)(addr >> 24);
    req[1] = (uint8_t)(addr >> 16);
    req[2] = (uint8_t)(addr >> 8);
    req[3] = (uint8_t)addr;
    req[4] = (uint8_t)(len >> 8);
    req[5] = (uint8_t)len;
    memcpy(req + 6, data, len);

    pst = proto_send(proto, ECU_FILE_SID_WRITE_FLASH, req, (size_t)(6 + len),
                     resp, sizeof(resp), &resp_len, timeout_ms);
    if (pst != PROTO_OK) {
        return ecu_file_from_proto(pst);
    }
    if (resp_len < 1 ||
        resp[0] != (uint8_t)(ECU_FILE_SID_WRITE_FLASH + 0x40U))
    {
        return ECU_FILE_ERR_PROTOCOL;
    }
    return ECU_FILE_OK;
}

/* True when a directory entry belongs to ecu_id's backup file. */
static bool ecu_file_name_matches(const char *name, const char *ecu_id)
{
    char with_ext[ECU_FILE_PATH_LEN];

    if (name == NULL || ecu_id == NULL) {
        return false;
    }
    if (strcmp(name, ecu_id) == 0) {
        return true;
    }
    if (snprintf(with_ext, sizeof(with_ext), "%s%s", ecu_id,
                 ECU_FILE_BACKUP_EXT) > 0 &&
        strcmp(name, with_ext) == 0)
    {
        return true;
    }
    /* Full path (e.g. /ecu/backup/<ecu_id>.bin) contains the id. */
    if (strstr(name, ecu_id) != NULL) {
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

ecu_file_status_t ecu_file_init(const ecu_file_config_t *cfg,
                                ecu_file_t **out)
{
    if (out == NULL) {
        return ECU_FILE_ERR_INVALID_ARG;
    }
    if (cfg == NULL) {
        return ECU_FILE_ERR_INVALID_ARG;
    }
    if (!sd_is_initialized()) {
        return ECU_FILE_ERR_SD;
    }

    ecu_file_t *ef = (ecu_file_t *)calloc(1, sizeof(*ef));
    if (ef == NULL) {
        return ECU_FILE_ERR_INVALID_ARG;
    }

    ef->cfg         = *cfg;   /* shallow copy: config is empty by design */
    ef->initialized = true;

    *out = ef;
    return ECU_FILE_OK;
}

ecu_file_status_t ecu_file_deinit(ecu_file_t *ef)
{
    if (ef == NULL) {
        return ECU_FILE_ERR_INVALID_ARG;
    }
    ef->initialized = false;
    free(ef);
    return ECU_FILE_OK;
}

/* ------------------------------------------------------------------------
 * Backup / restore
 * ------------------------------------------------------------------------ */

ecu_file_status_t ecu_file_backup(ecu_file_t *ef, const char *ecu_id,
                                  protocol_t *proto, uint32_t timeout_ms)
{
    ecu_file_status_t st;
    sd_status_t       sst;
    size_t            image_len = 0;
    uint32_t          addr = 0;
    char              path[ECU_FILE_PATH_LEN];

    st = ecu_file_check(ef, ecu_id, proto);
    if (st != ECU_FILE_OK) {
        return st;
    }

    /* Read the ECU flash image in chunks, bounded by ECU_FILE_MAX_IMAGE. */
    for (;;) {
        uint32_t remaining = ECU_FILE_MAX_IMAGE - image_len;
        uint16_t want      = (uint16_t)(remaining < ECU_FILE_CHUNK_SIZE
                                              ? remaining : ECU_FILE_CHUNK_SIZE);
        size_t   got = 0;

        if (want == 0) {
            return ECU_FILE_ERR_SD; /* image larger than we can hold */
        }
        st = ecu_file_read_chunk(proto, addr, want,
                                 ecu_file_image + image_len, &got, timeout_ms);
        if (st != ECU_FILE_OK) {
            return st;
        }
        if (got == 0) {
            break; /* end of flash image */
        }
        image_len += got;
        addr += got;
        if (got < want) {
            break; /* short chunk: end of flash image */
        }
    }

    if (image_len == 0) {
        return ECU_FILE_ERR_PROTOCOL; /* nothing read from the ECU */
    }

    sd_ecu_backup_path(ecu_id, path, sizeof(path));
    sst = sd_save_ecu_backup(ecu_id, ecu_file_image, image_len,
                             path, sizeof(path));
    if (sst != SD_OK) {
        return ecu_file_from_sd(sst);
    }

    /* Save CRC32 sidecar file for integrity verification on restore. */
    {
        uint32_t image_crc = crc32(ecu_file_image, image_len);
        char     crc_path[ECU_FILE_PATH_LEN];
        size_t   path_len = strlen(path);
        if (path_len > 4 && strcmp(path + path_len - 4, ".bin") == 0) {
            snprintf(crc_path, sizeof(crc_path), "%.*s.crc",
                     (int)(path_len - 4), path);
        } else {
            snprintf(crc_path, sizeof(crc_path), "%s.crc", path);
        }

        sd_file_t *crc_file = NULL;
        sst = sd_open(crc_path, "w", &crc_file);
        if (sst == SD_OK && crc_file != NULL) {
            uint8_t crc_buf[4];
            crc_buf[0] = (uint8_t)(image_crc >> 24);
            crc_buf[1] = (uint8_t)(image_crc >> 16);
            crc_buf[2] = (uint8_t)(image_crc >> 8);
            crc_buf[3] = (uint8_t)image_crc;
            (void)sd_write(crc_file, crc_buf, sizeof(crc_buf));
            (void)sd_flush(crc_file);
            sd_close(crc_file);
        }
    }

    return ECU_FILE_OK;
}

ecu_file_status_t ecu_file_restore(ecu_file_t *ef, const char *ecu_id,
                                   protocol_t *proto, display_t *disp,
                                   uint32_t timeout_ms)
{
    ecu_file_status_t st;
    sd_status_t       sst;
    char            **names = NULL;
    size_t            count = 0;
    size_t            len = 0;
    uint32_t          addr = 0;
    char              path[ECU_FILE_PATH_LEN];
    size_t            i;
    bool              found = false;

    st = ecu_file_check(ef, ecu_id, proto);
    if (st != ECU_FILE_OK) {
        return st;
    }
    if (disp == NULL) {
        return ECU_FILE_ERR_INVALID_ARG;
    }

    /* 1. The backup must exist on SD. */
    sst = sd_list_ecu_backups(&names, &count);
    if (sst != SD_OK) {
        return ecu_file_from_sd(sst);
    }
    for (i = 0; i < count; i++) {
        if (ecu_file_name_matches(names[i], ecu_id)) {
            found = true;
            break;
        }
    }
    sd_readdir_free(names, count);
    if (!found) {
        return ECU_FILE_ERR_NO_BACKUP;
    }

    /* 2. Safety: confirm before flashing (SELECT = proceed). */
    if (!warning_show(disp, WARN_ECU_FLASH, NULL, timeout_ms)) {
        return ECU_FILE_ERR_INVALID_ARG; /* user declined (BACK / timeout) */
    }

    /* 3. Load the backup image from SD. */
    sd_ecu_backup_path(ecu_id, path, sizeof(path));
    sst = sd_load_ecu_backup(path, ecu_file_image, sizeof(ecu_file_image),
                             &len);
    if (sst != SD_OK) {
        return ecu_file_from_sd(sst);
    }
    if (len == 0 || len > sizeof(ecu_file_image)) {
        return ECU_FILE_ERR_SD;
    }

    /* 3b. Verify CRC32 sidecar before trusting the image. */
    {
        char     crc_path[ECU_FILE_PATH_LEN];
        size_t   path_len = strlen(path);
        if (path_len > 4 && strcmp(path + path_len - 4, ".bin") == 0) {
            snprintf(crc_path, sizeof(crc_path), "%.*s.crc",
                     (int)(path_len - 4), path);
        } else {
            snprintf(crc_path, sizeof(crc_path), "%s.crc", path);
        }

        sd_file_t *crc_file = NULL;
        sst = sd_open(crc_path, "r", &crc_file);
        if (sst != SD_OK || crc_file == NULL) {
            return ECU_FILE_ERR_INTEGRITY; /* no CRC sidecar: refuse restore */
        }

        uint8_t crc_buf[4];
        size_t  crc_len = 0;
        sst = sd_read(crc_file, crc_buf, sizeof(crc_buf), &crc_len);
        sd_close(crc_file);
        if (sst != SD_OK || crc_len != sizeof(crc_buf)) {
            return ECU_FILE_ERR_INTEGRITY;
        }

        uint32_t stored_crc = ((uint32_t)crc_buf[0] << 24)
                            | ((uint32_t)crc_buf[1] << 16)
                            | ((uint32_t)crc_buf[2] << 8)
                            |  (uint32_t)crc_buf[3];
        uint32_t image_crc  = crc32(ecu_file_image, len);
        if (image_crc != stored_crc) {
            return ECU_FILE_ERR_INTEGRITY; /* image corrupted or tampered */
        }
    }

    /* 4. Program the ECU flash in chunks. */
    while (addr < len) {
        uint32_t remaining = len - addr;
        uint16_t want      = (uint16_t)(remaining < ECU_FILE_CHUNK_SIZE
                                              ? remaining : ECU_FILE_CHUNK_SIZE);
        st = ecu_file_write_chunk(proto, addr, ecu_file_image + addr, want,
                                  timeout_ms);
        if (st != ECU_FILE_OK) {
            return st;
        }
        addr += want;
    }

    return ECU_FILE_OK;
}

ecu_file_status_t ecu_file_list_backups(char ***out_names, size_t *out_count)
{
    sd_status_t st;

    if (out_names == NULL || out_count == NULL) {
        return ECU_FILE_ERR_INVALID_ARG;
    }
    if (!sd_is_initialized()) {
        return ECU_FILE_ERR_SD;
    }

    st = sd_list_ecu_backups(out_names, out_count);
    return ecu_file_from_sd(st);
}
