#ifndef LUPI_ZIP_H
#define LUPI_ZIP_H

#include <stddef.h>

/* Writable directory used when TMPDIR cannot be created. Copied immediately. */
void lupi_set_extract_root(const char *root);

int extract_lupi_to_tmp(const char *lupi_path, char *out_dir, size_t out_dir_size);
void cleanup_lupi_tmp(const char *tmp_dir);

/* Last failure from extract_lupi_to_tmp. Empty if the last call succeeded. */
const char *lupi_zip_error(void);

#endif
