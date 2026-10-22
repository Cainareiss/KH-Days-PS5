/* Native runtime paths. Configure these before khdays_ps5_boot/rom_init. */
#ifndef KHDAYS_PATHS_PS5_H
#define KHDAYS_PATHS_PS5_H

/* NULL keeps a default. All supplied paths must be absolute. Returns 0
 * without changing any path when an argument is invalid or too long. */
int khdays_ps5_set_paths(const char *data, const char *saves, const char *mods);
const char *khdays_ps5_data_root(void);
const char *khdays_ps5_save_root(void);
const char *khdays_ps5_mods_root(void);

#endif
