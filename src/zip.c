#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <stdint.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#if defined(_WIN32)
#include <direct.h>
static int lupi_mkdir(const char *path) { return _mkdir(path); }
#else
static int lupi_mkdir(const char *path) { return mkdir(path, 0755); }
#endif

#include <zlib.h>

#include "zip.h"

static char zip_error[512];
static char extract_root[512];

static void set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(zip_error, sizeof(zip_error), fmt, args);
    va_end(args);
    fprintf(stderr, "%s\n", zip_error);
}

void lupi_set_extract_root(const char *root) {
    if (!root || !root[0]) {
        extract_root[0] = '\0';
        return;
    }
    snprintf(extract_root, sizeof(extract_root), "%s", root);
}

const char *lupi_zip_error(void) {
    return zip_error;
}


static int create_parent_directories(const char *filepath) {
    char *path_copy = strdup(filepath);
    if (!path_copy) return -1;

    char *p = path_copy;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            if (strlen(path_copy) > 0) {
                if (lupi_mkdir(path_copy) != 0 && errno != EEXIST) {
                    free(path_copy);
                    return -1;
                }
            }
            *p = '/';
        }
        p++;
    }

    free(path_copy);
    return 0;
}

static int is_path_safe(const char *base_dir, const char *filepath) {
    // Check for path traversal attempts
    if (strstr(filepath, "..") != NULL) {
        return 0;
    }

    // Ensure filepath starts with base_dir
    size_t base_len = strlen(base_dir);
    if (strncmp(filepath, base_dir, base_len) != 0) {
        return 0;
    }

    return 1;
}

static int parent_of(const char *path, char *out, size_t out_size) {
    size_t len = strlen(path);
    size_t slash;

    while (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\')) len--;
    slash = len;
    while (slash > 0 && path[slash - 1] != '/' && path[slash - 1] != '\\') slash--;
    if (slash == 0) return -1;

    len = slash - 1;
    if (len == 0) {
        if (out_size < 2) return -1;
        memcpy(out, "/", 2);
        return 0;
    }
    if (len + 1 > out_size) return -1;
    memcpy(out, path, len);
    out[len] = '\0';
    return 0;
}

static int try_make_under(const char *root, char *out_dir, size_t out_dir_size) {
    size_t n;
    int i;

    if (!root || !root[0]) return -1;
    n = strlen(root);
    while (n > 0 && (root[n - 1] == '/' || root[n - 1] == '\\')) n--;
    if (n == 0) return -1;

    for (i = 0; i < 128; i++) {
        int written = snprintf(out_dir, out_dir_size, "%.*s/lupi-%ld-%d",
                               (int)n, root, (long)getpid(), i);
        if (written < 0 || (size_t)written >= out_dir_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (lupi_mkdir(out_dir) == 0) return 0;
        if (errno != EEXIST) return -1;
    }
    errno = EEXIST;
    return -1;
}

/* Android apps cannot create /tmp. Prefer a directory RetroArch already
 * grants, then the usual environment variables, then the folder that holds
 * the .lupi, and only then /tmp. */
static int make_temp_dir(const char *lupi_path, char *out_dir, size_t out_dir_size) {
    const char *roots[6];
    char content_dir[PATH_MAX];
    char detail[384];
    int n = 0;
    int r;
    const char *env;

    detail[0] = '\0';

    if (extract_root[0]) roots[n++] = extract_root;

    env = getenv("TMPDIR");
    if (env && env[0]) roots[n++] = env;
    env = getenv("TEMP");
    if (env && env[0]) roots[n++] = env;
    env = getenv("TMP");
    if (env && env[0]) roots[n++] = env;

    if (lupi_path && parent_of(lupi_path, content_dir, sizeof(content_dir)) == 0)
        roots[n++] = content_dir;

    roots[n++] = "/tmp";

    for (r = 0; r < n; r++) {
        if (try_make_under(roots[r], out_dir, out_dir_size) == 0) return 0;
        {
            int err = errno;
            size_t used = strlen(detail);
            if (used + 1 < sizeof(detail)) {
                snprintf(detail + used, sizeof(detail) - used, "%s%s (%s)",
                         used ? "; " : "", roots[r], strerror(err));
            }
        }
    }

    set_error("Failed to create temp directory: %s", detail);
    return -1;
}

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static int inflate_entry(FILE *in, FILE *out, uint32_t comp_size, uint32_t uncomp_size) {
    z_stream strm;
    uint8_t inbuf[4096];
    uint8_t outbuf[4096];
    int ret;
    uint32_t remaining = comp_size;
    uint32_t written = 0;

    memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) return -1;

    do {
        size_t n = remaining > sizeof(inbuf) ? sizeof(inbuf) : remaining;
        if (n == 0) break;
        if (fread(inbuf, 1, n, in) != n) {
            inflateEnd(&strm);
            return -1;
        }
        remaining -= (uint32_t)n;
        strm.next_in = inbuf;
        strm.avail_in = (uInt)n;

        do {
            strm.next_out = outbuf;
            strm.avail_out = sizeof(outbuf);
            ret = inflate(&strm, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                inflateEnd(&strm);
                return -1;
            }
            size_t have = sizeof(outbuf) - strm.avail_out;
            if (fwrite(outbuf, 1, have, out) != have) {
                inflateEnd(&strm);
                return -1;
            }
            written += (uint32_t)have;
        } while (strm.avail_out == 0);
    } while (ret != Z_STREAM_END && remaining > 0);

    inflateEnd(&strm);
    (void)uncomp_size;
    return (ret == Z_STREAM_END || written > 0) ? 0 : -1;
}

int extract_lupi_to_tmp(const char *lupi_path, char *out_dir, size_t out_dir_size) {
    FILE *zip = fopen(lupi_path, "rb");
    int files_extracted = 0;
    int ret = 0;

    zip_error[0] = '\0';

    if (!zip) {
        set_error("Failed to open %s: %s", lupi_path, strerror(errno));
        return 1;
    }

    if (make_temp_dir(lupi_path, out_dir, out_dir_size) != 0) {
        fclose(zip);
        return 1;
    }

    for (;;) {
        uint8_t header[30];
        size_t n = fread(header, 1, 30, zip);
        if (n < 30) break;

        uint32_t sig = read_u32(header);
        if (sig == 0x02014b50u || sig == 0x06054b50u) break;
        if (sig != 0x04034b50u) {
            set_error("Invalid zip signature in %s", lupi_path);
            ret = 1;
            break;
        }

        uint16_t flags = read_u16(header + 6);
        uint16_t method = read_u16(header + 8);
        uint32_t comp_size = read_u32(header + 18);
        uint32_t uncomp_size = read_u32(header + 22);
        uint16_t namelen = read_u16(header + 26);
        uint16_t extra = read_u16(header + 28);

        char *name = (char *)malloc((size_t)namelen + 1);
        if (!name || fread(name, 1, namelen, zip) != namelen) {
            free(name);
            ret = 1;
            break;
        }
        name[namelen] = '\0';

        if (extra > 0) fseek(zip, extra, SEEK_CUR);

        if ((flags & 0x8) != 0) {
            set_error("Unsupported zip data descriptor in %s", name);
            free(name);
            ret = 1;
            break;
        }

        char filepath[PATH_MAX];
        snprintf(filepath, sizeof(filepath), "%s/%s", out_dir, name);
        if (!is_path_safe(out_dir, filepath)) {
            set_error("Skipping unsafe path: %s", name);
            fseek(zip, (long)comp_size, SEEK_CUR);
            free(name);
            continue;
        }

        int is_dir = (namelen > 0 && name[namelen - 1] == '/') ||
                     (uncomp_size == 0 && method == 0 && comp_size == 0);
        if (is_dir) {
            if (create_parent_directories(filepath) != 0) ret = 1;
            lupi_mkdir(filepath);
            free(name);
            files_extracted++;
            continue;
        }

        if (create_parent_directories(filepath) != 0) {
            set_error("Failed to create directories for: %s: %s", filepath, strerror(errno));
            fseek(zip, (long)comp_size, SEEK_CUR);
            free(name);
            continue;
        }

        FILE *out = fopen(filepath, "wb");
        if (!out) {
            set_error("Failed to write %s: %s", filepath, strerror(errno));
            fseek(zip, (long)comp_size, SEEK_CUR);
            free(name);
            ret = 1;
            continue;
        }

        if (method == 0) {
            uint8_t buf[4096];
            uint32_t left = comp_size;
            while (left > 0) {
                size_t chunk = left > sizeof(buf) ? sizeof(buf) : left;
                if (fread(buf, 1, chunk, zip) != chunk || fwrite(buf, 1, chunk, out) != chunk) {
                    ret = 1;
                    break;
                }
                left -= (uint32_t)chunk;
            }
        } else if (method == 8) {
            if (inflate_entry(zip, out, comp_size, uncomp_size) != 0) ret = 1;
        } else {
            set_error("Unsupported compression method %u in %s", method, name);
            fseek(zip, (long)comp_size, SEEK_CUR);
            ret = 1;
        }

        fclose(out);
        free(name);
        files_extracted++;
    }

    fclose(zip);
    if (files_extracted == 0) {
        set_error("No files extracted from %s", lupi_path);
        ret = 1;
    } else if (ret != 0 && zip_error[0] == '\0') {
        set_error("Failed to extract %s", lupi_path);
    }
    return ret;
}

static int remove_tree(const char *path) {
    struct stat st;
    DIR *dir;
    struct dirent *ent;

    if (stat(path, &st) != 0) return -1;

    if ((st.st_mode & S_IFDIR) == 0) {
        return unlink(path);
    }

    dir = opendir(path);
    if (!dir) return -1;
    while ((ent = readdir(dir)) != NULL) {
        char child[PATH_MAX];
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        remove_tree(child);
    }
    closedir(dir);
    return rmdir(path);
}

void cleanup_lupi_tmp(const char *tmp_dir) {
    if (!tmp_dir || tmp_dir[0] == '\0') return;
    remove_tree(tmp_dir);
}
