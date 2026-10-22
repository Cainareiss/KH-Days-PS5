/* Cartridge reader for the real khdays-port runtime (rom.h), in C/POSIX.
 * System blobs/FAT are resident; NitroFS files are streamed with a reader
 * cache per thread. No SDL, Win32, C++ filesystem, or reconstructed gameplay.
 *
 * Like upstream rom.cpp this reconstructs the card's original address space
 * from an extraction. Mod files are selected alphabetically, and differently
 * sized replacements move past the ROM's used end with the FAT updated. */
#include "rom_ps5.h"
#include "host.h"
#include "paths_ps5.h"
#include "origin.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef KHDAYS_ROM_NO_DIRECTORY_SCAN
#include <dirent.h>
#endif

#define PATH_ROOM 1024u
#define OPEN_FILES 64u
#define MAX_BLOB (16u * 1024u * 1024u)
#define MAX_UNPACKED (256u * 1024u * 1024u)
enum { HEADER, SYSTEM_BLOB, FAT, GAME_FILE, UNEXTRACTED };

struct Blob { u32 start, size; u8 *bytes; };
struct GameFile { char *name, *path; u32 size; };
struct Region { u32 start, end; int kind, index; };
struct NamedFile { const char *name; u32 id; };
struct OpenFile { FILE *stream; int id; u32 next; unsigned long long used; };
struct KhdaysRomReader {
    struct OpenFile open[OPEN_FILES];
    unsigned long long used;
};

static u8 header[0x200];
static u8 *fat;
static u32 fat_size, file_count;
static struct Blob blobs[4];
static struct GameFile *files;
static struct NamedFile *names;
static struct Region *regions;
static size_t region_count;
static char data_root[PATH_ROOM];
static char **mod_roots;
static size_t mod_count;
static struct KhdaysRomReader card_reader;
static int initialized;

static u16 le16(const u8 *at) { return (u16)(at[0] | (u16)at[1] << 8); }
static u32 le32(const u8 *at)
{
    return (u32)at[0] | (u32)at[1] << 8 | (u32)at[2] << 16 | (u32)at[3] << 24;
}
static void put32(u8 *at, u32 value)
{
    for (unsigned i = 0; i < 4; ++i) {
        at[i] = (u8)(value >> (8u * i));
    }
}

static char *copy_string(const char *text)
{
    const size_t length = strlen(text) + 1u;
    char *copy = malloc(length);
    if (copy != NULL) {
        memcpy(copy, text, length);
    }
    return copy;
}

static int join(char *out, const char *root, const char *relative)
{
    const int length = snprintf(out, PATH_ROOM, "%s/%s", root, relative);
    return length >= 0 && (size_t)length < PATH_ROOM;
}

static int file_size(const char *path, u32 *size)
{
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_size < 0 || (unsigned long long)info.st_size > UINT_MAX) {
        return 0;
    }
    *size = (u32)info.st_size;
    return 1;
}

static int extracted(const char *path)
{
    char test[PATH_ROOM];
    u32 size;
    return join(test, path, "system/header.bin") && file_size(test, &size) && size == sizeof(header);
}

static int report(const char *what)
{
    fprintf(stderr, "rom: %s\n", what);
    khdays_host_tell_no_data(data_root[0] ? data_root : khdays_ps5_data_root(), what);
    return 0;
}

static void fatal(const char *what)
{
    (void)report(what);
    fflush(stderr);
    exit(8);
}

static void close_reader(KhdaysRomReader *reader)
{
    for (size_t i = 0; i < OPEN_FILES; ++i) {
        if (reader->open[i].stream != NULL) {
            fclose(reader->open[i].stream);
        }
    }
    memset(reader, 0, sizeof(*reader));
}

/* Upstream rom.h has no reader-free counterpart. */
void khdays_ps5_rom_reader_delete(KhdaysRomReader *reader)
{
    if (reader != NULL && reader != &card_reader) {
        close_reader(reader);
        free(reader);
    }
}

static void discard(void)
{
    close_reader(&card_reader);
    for (u32 id = 0; files != NULL && id < file_count; ++id) {
        free(files[id].name);
        free(files[id].path);
    }
    for (size_t i = 0; i < 4; ++i) {
        free(blobs[i].bytes);
    }
    for (size_t i = 0; i < mod_count; ++i) {
        free(mod_roots[i]);
    }
    free(mod_roots);
    free(files);
    free(names);
    free(regions);
    free(fat);
    memset(blobs, 0, sizeof(blobs));
    memset(header, 0, sizeof(header));
    fat = NULL;
    files = NULL;
    names = NULL;
    regions = NULL;
    mod_roots = NULL;
    mod_count = region_count = 0;
    fat_size = file_count = 0;
    initialized = 0;
}

#ifndef KHDAYS_ROM_NO_DIRECTORY_SCAN
static int string_order(const void *left, const void *right)
{
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static int child_directories(const char *root, char ***out, size_t *count)
{
    DIR *directory = opendir(root);
    struct dirent *entry;
    *out = NULL;
    *count = 0;
    if (directory == NULL) {
        return 1; /* Optional mods may be absent; no data is reported later. */
    }
    while ((entry = readdir(directory)) != NULL) {
        char path[PATH_ROOM];
        struct stat info;
        const size_t length = strlen(entry->d_name);
        char **grown;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            (length >= 8 && strcmp(entry->d_name + length - 8, ".partial") == 0) ||
            !join(path, root, entry->d_name) || stat(path, &info) != 0 || !S_ISDIR(info.st_mode)) {
            continue;
        }
        grown = realloc(*out, (*count + 1u) * sizeof(**out));
        if (grown == NULL) {
            closedir(directory);
            return 0;
        }
        *out = grown;
        (*out)[*count] = copy_string(path);
        if ((*out)[*count] == NULL) {
            closedir(directory);
            return 0;
        }
        ++*count;
    }
    closedir(directory);
    if (*count > 1) {
        qsort(*out, *count, sizeof(**out), string_order);
    }
    return 1;
}
#endif

static int choose_data_root(void)
{
    const char *root = khdays_ps5_data_root();
    if (extracted(root)) {
        memcpy(data_root, root, strlen(root) + 1u);
        return 1;
    }
#ifndef KHDAYS_ROM_NO_DIRECTORY_SCAN
    char **children = NULL;
    size_t count = 0;
    int found = 0;
    const int scanned = child_directories(root, &children, &count);
    for (size_t i = 0; i < count; ++i) {
        if (!found && extracted(children[i])) {
            memcpy(data_root, children[i], strlen(children[i]) + 1u);
            found = 1;
        }
        free(children[i]);
    }
    free(children);
    return scanned && found;
#else
    return 0;
#endif
}

static int load_system(const char *name, u32 expected, u8 **out)
{
    char relative[96], path[PATH_ROOM];
    u32 actual;
    FILE *stream;
    u8 *bytes;
    snprintf(relative, sizeof(relative), "system/%s", name);
    if (expected > MAX_BLOB || !join(path, data_root, relative) ||
        !file_size(path, &actual) || actual != expected) {
        return report("a system file is absent or its length disagrees with the cartridge header");
    }
    bytes = malloc(expected ? expected : 1u);
    if (bytes == NULL) {
        return report("not enough memory for the system files");
    }
    stream = fopen(path, "rb");
    if (stream == NULL) {
        free(bytes);
        return report("cannot open a system file");
    }
    const size_t got = fread(bytes, 1, expected, stream);
    const int closed = fclose(stream);
    if (got != expected || closed != 0) {
        free(bytes);
        return report("a system file could not be read completely");
    }
    *out = bytes;
    return 1;
}

static int safe_relative(const char *name)
{
    const char *part = name;
    if (*part == '\0' || *part == '/') {
        return 0;
    }
    while (*part != '\0') {
        const char *end = part;
        while (*end != '\0' && *end != '/') {
            if (*end == '\\' || *end == ':') {
                return 0;
            }
            ++end;
        }
        if (end == part || (end - part == 1 && part[0] == '.') ||
            (end - part == 2 && part[0] == '.' && part[1] == '.')) {
            return 0;
        }
        if (*end == '\0') {
            return 1;
        }
        part = end + 1;
    }
    return 0;
}

int khdays_mods_find(const char *relative, char *path, int size)
{
    char candidate[PATH_ROOM];
    u32 bytes;
    if (relative == NULL || path == NULL || size <= 0 || !safe_relative(relative)) {
        return 0;
    }
    for (size_t i = 0; i < mod_count; ++i) {
        if (join(candidate, mod_roots[i], relative) && file_size(candidate, &bytes)) {
            const size_t length = strlen(candidate) + 1u;
            if (length > (size_t)size) {
                return 0;
            }
            memcpy(path, candidate, length);
            return 1;
        }
    }
    return 0;
}

/* A bounded FNT walk: reject cycles, truncation, duplicate ids and paths
 * that could escape the extraction before ever touching such a file. */
static int walk_fnt(u32 directory, const char *prefix, u8 *visited, u32 directories)
{
    const struct Blob *fnt = &blobs[3];
    const u32 number = directory - 0xf000u;
    u32 at, id;
    if (directory < 0xf000u || number >= directories || visited[number]) {
        return 0;
    }
    visited[number] = 1;
    at = le32(fnt->bytes + 8u * number);
    id = le16(fnt->bytes + 8u * number + 4u);
    while (at < fnt->size) {
        char path[PATH_ROOM], component[128];
        const u8 type = fnt->bytes[at++];
        const u32 length = type & 0x7fu;
        if (type == 0) {
            return 1;
        }
        if (length == 0 || length > fnt->size - at) {
            return 0;
        }
        memcpy(component, fnt->bytes + at, length);
        component[length] = '\0';
        at += length;
        if (strlen(component) != length || strchr(component, '/') != NULL || !safe_relative(component) ||
            snprintf(path, sizeof(path), "%s%s", prefix, component) >= (int)sizeof(path)) {
            return 0;
        }
        if ((type & 0x80u) != 0) {
            const size_t used = strlen(path);
            if (fnt->size - at < 2 || used + 2u > sizeof(path)) {
                return 0;
            }
            path[used] = '/';
            path[used + 1u] = '\0';
            const u32 child = le16(fnt->bytes + at);
            at += 2;
            if (!walk_fnt(child, path, visited, directories)) {
                return 0;
            }
        } else {
            if (id >= file_count || files[id].name != NULL) {
                return 0;
            }
            files[id].name = copy_string(path);
            if (files[id].name == NULL) {
                return 0;
            }
            ++id;
        }
    }
    return 0;
}

static void region(u32 start, u32 size, int kind, int index)
{
    if (size != 0) {
        regions[region_count++] = (struct Region){start, start + size, kind, index};
    }
}
static int region_order(const void *left, const void *right)
{
    const struct Region *a = left, *b = right;
    return a->start < b->start ? -1 : a->start != b->start;
}
static int name_order(const void *left, const void *right)
{
    const struct NamedFile *a = left, *b = right;
    return strcmp(a->name, b->name);
}

int khdays_rom_init(void)
{
    static const struct { const char *name; u32 offset, size; } system[] = {
        {"arm9.bin", 0x20, 0x2c}, {"arm7.bin", 0x30, 0x3c},
        {"arm9_overlay_table.bin", 0x50, 0x54}, {"fnt.bin", 0x40, 0x44}
    };
    u8 *bytes = NULL, *visited = NULL;
    u32 rom_end, directories;
    if (initialized) {
        return 1;
    }
    discard();
    data_root[0] = '\0';
    if (!choose_data_root()) {
        return report("no complete extracted cartridge under the configured data root");
    }
    if (!load_system("header.bin", sizeof(header), &bytes)) {
        goto fail;
    }
    memcpy(header, bytes, sizeof(header));
    free(bytes);
    bytes = NULL;
    /* Every compiled function and DS symbol is for the European revision 0.
     * Taking a USA image here would load bytes at the wrong compiled addresses. */
    if (memcmp(header + 0x0c, "YKGP", 4) != 0 || header[0x1e] != 0 ||
        le32(header + 0x28) != 0x02000000u) {
        report("this native build requires the YKGP European revision 0 extraction");
        goto fail;
    }
    fat_size = le32(header + 0x4c);
    file_count = fat_size / 8u;
    if (fat_size == 0 || (fat_size & 7u) != 0 || file_count > 65536u) {
        report("invalid FAT length");
        goto fail;
    }
    files = calloc(file_count, sizeof(*files));
    names = calloc(file_count, sizeof(*names));
    regions = calloc((size_t)file_count + 10u, sizeof(*regions));
    if (files == NULL || names == NULL || regions == NULL) {
        report("not enough memory for the cartridge index");
        goto fail;
    }
    region(0, sizeof(header), HEADER, 0);
    for (size_t i = 0; i < 4; ++i) {
        blobs[i].start = le32(header + system[i].offset);
        blobs[i].size = le32(header + system[i].size);
        if (blobs[i].size > UINT_MAX - blobs[i].start ||
            !load_system(system[i].name, blobs[i].size, &blobs[i].bytes)) {
            goto fail;
        }
        region(blobs[i].start, blobs[i].size, SYSTEM_BLOB, (int)i);
    }
    if (!load_system("fat.bin", fat_size, &fat) ||
        fat_size > UINT_MAX - le32(header + 0x48)) {
        goto fail;
    }
    region(le32(header + 0x48), fat_size, FAT, 0);
    region(0x200, 0x3e00, UNEXTRACTED, 0);
    if (blobs[0].start + blobs[0].size < blobs[2].start) {
        region(blobs[0].start + blobs[0].size,
               blobs[2].start - (blobs[0].start + blobs[0].size), UNEXTRACTED, 0);
    }
    if (le32(header + 0x68) != 0) {
        if (le32(header + 0x68) > UINT_MAX - 0x840u) {
            report("invalid banner range");
            goto fail;
        }
        region(le32(header + 0x68), 0x840, UNEXTRACTED, 0);
    }
    if (blobs[3].size < 8) {
        report("truncated FNT root");
        goto fail;
    }
    directories = le16(blobs[3].bytes + 6);
    if (directories == 0 || directories > 4096u || directories * 8u > blobs[3].size) {
        report("invalid FNT directory count");
        goto fail;
    }
    visited = calloc(directories, 1);
    if (visited == NULL || !walk_fnt(0xf000, "", visited, directories)) {
        report("invalid or truncated FNT directory tree");
        goto fail;
    }
    free(visited);
    visited = NULL;
#ifndef KHDAYS_ROM_NO_DIRECTORY_SCAN
    if (!child_directories(khdays_ps5_mods_root(), &mod_roots, &mod_count)) {
        report("cannot index mod folders");
        goto fail;
    }
#endif
    rom_end = le32(header + 0x80);
    for (u32 id = 0; id < file_count; ++id) {
        char relative[PATH_ROOM], path[PATH_ROOM];
        const u32 start = le32(fat + 8u * id), end = le32(fat + 8u * id + 4u);
        u32 placed = start, size;
        int modified;
        if (end < start || end > rom_end) {
            report("a FAT file range lies outside the cartridge");
            goto fail;
        }
        if (files[id].name == NULL) {
            snprintf(relative, sizeof(relative), "_unnamed/file_%05u.bin", id);
            files[id].name = copy_string(relative);
        }
        if (files[id].name == NULL || !join(relative, "files", files[id].name)) {
            report("an extracted filename is too long");
            goto fail;
        }
        modified = khdays_mods_find(relative, path, sizeof(path));
        if (!modified && (!join(relative, "nitrofs", files[id].name) ||
                          !join(path, data_root, relative))) {
            report("an extracted path is too long");
            goto fail;
        }
        if (!file_size(path, &size)) {
            fprintf(stderr, "rom: missing or unreadable: %s\n", path);
            report("an extracted NitroFS file is missing");
            goto fail;
        }
        if (!modified && size != end - start) {
            report("an original NitroFS file length disagrees with the FAT");
            goto fail;
        }
        files[id].path = copy_string(path);
        files[id].size = size;
        if (files[id].path == NULL) {
            report("not enough memory for an extracted path");
            goto fail;
        }
        if (size != end - start) {
            if (rom_end > UINT_MAX - 0x1ffu || size > UINT_MAX - ((rom_end + 0x1ffu) & ~0x1ffu)) {
                report("mod files exceed the card's 32-bit address space");
                goto fail;
            }
            placed = (rom_end + 0x1ffu) & ~0x1ffu;
            rom_end = placed + size;
            put32(fat + 8u * id, placed);
            put32(fat + 8u * id + 4u, placed + size);
        }
        region(placed, size, GAME_FILE, (int)id);
        names[id] = (struct NamedFile){files[id].name, id};
        if (modified && strncmp(files[id].name, "mi/wd/wd_", 9) == 0) {
            khdays_origin_want();
        }
    }
    qsort(names, file_count, sizeof(*names), name_order);
    qsort(regions, region_count, sizeof(*regions), region_order);
    for (size_t i = 1; i < region_count; ++i) {
        if (regions[i - 1u].end > regions[i].start) {
            report("overlapping regions in the extracted cartridge");
            goto fail;
        }
    }
    initialized = 1;
    fprintf(stderr, "rom: %u files indexed from %s (%zu mod folders)\n", file_count, data_root, mod_count);
    return 1;
fail:
    free(bytes);
    free(visited);
    discard();
    return 0;
}

const u8 *khdays_rom_header(void) { return header; }

/* The first region starting after offset, equivalent to upper_bound. */
static size_t after(u32 offset)
{
    size_t low = 0, high = region_count;
    while (low < high) {
        const size_t mid = low + (high - low) / 2u;
        if (regions[mid].start <= offset) {
            low = mid + 1u;
        } else {
            high = mid;
        }
    }
    return low;
}

static struct OpenFile *handle(KhdaysRomReader *reader, int id)
{
    struct OpenFile *oldest = &reader->open[0];
    for (size_t i = 0; i < OPEN_FILES; ++i) {
        struct OpenFile *open = &reader->open[i];
        if (open->stream != NULL && open->id == id) {
            open->used = ++reader->used;
            return open;
        }
        if (open->stream == NULL || (oldest->stream != NULL && open->used < oldest->used)) {
            oldest = open;
        }
    }
    if (oldest->stream != NULL) {
        fclose(oldest->stream);
    }
    oldest->stream = fopen(files[id].path, "rb");
    if (oldest->stream == NULL) {
        fatal("cannot open an extracted game file");
    }
    (void)setvbuf(oldest->stream, NULL, _IOFBF, 64u * 1024u);
    oldest->id = id;
    oldest->next = 0;
    oldest->used = ++reader->used;
    return oldest;
}

static void read_rom(KhdaysRomReader *reader, u32 offset, u8 *out, u32 size)
{
    if (!initialized || reader == NULL || (size != 0 && out == NULL) || size > UINT_MAX - offset) {
        fatal("invalid cartridge read");
    }
    while (size != 0) {
        const size_t next = after(offset);
        const struct Region *r = next != 0 ? &regions[next - 1u] : NULL;
        u32 chunk;
        if (r != NULL && offset < r->end) {
            const u32 at = offset - r->start;
            chunk = r->end - offset < size ? r->end - offset : size;
            switch (r->kind) {
            case HEADER: memcpy(out, header + at, chunk); break;
            case SYSTEM_BLOB: memcpy(out, blobs[r->index].bytes + at, chunk); break;
            case FAT: memcpy(out, fat + at, chunk); break;
            case GAME_FILE: {
                struct OpenFile *file = handle(reader, r->index);
                if ((file->next != at && fseek(file->stream, (long)at, SEEK_SET) != 0) ||
                    fread(out, 1, chunk, file->stream) != chunk) {
                    fatal("short read or seek failure in an extracted game file");
                }
                file->next = at + chunk;
                break;
            }
            default:
                fprintf(stderr, "rom: unextracted cartridge bytes at 0x%08x\n", offset);
                fatal("the game read cartridge data the extraction does not keep");
            }
        } else {
            const u32 end = next < region_count ? regions[next].start : UINT_MAX;
            chunk = end - offset < size ? end - offset : size;
            memset(out, 0xff, chunk);
        }
        out += chunk;
        offset += chunk;
        size -= chunk;
    }
}

void khdays_rom_read(u32 offset, u8 *destination, u32 size)
{
    read_rom(&card_reader, offset, destination, size);
}
KhdaysRomReader *khdays_rom_reader_new(void)
{
    return calloc(1, sizeof(KhdaysRomReader));
}
void khdays_rom_reader_read(KhdaysRomReader *reader, u32 offset, u8 *destination, u32 size)
{
    read_rom(reader, offset, destination, size);
}

int khdays_rom_find(const char *path, u32 *offset, u32 *size)
{
    size_t low = 0, high = file_count;
    if (!initialized || path == NULL || offset == NULL || size == NULL) {
        return 0;
    }
    while (*path == '/') {
        ++path;
    }
    while (low < high) {
        const size_t mid = low + (high - low) / 2u;
        const int order = strcmp(path, names[mid].name);
        if (order == 0) {
            *offset = le32(fat + 8u * names[mid].id);
            *size = files[names[mid].id].size;
            return 1;
        }
        if (order > 0) { low = mid + 1u; } else { high = mid; }
    }
    return 0;
}

int khdays_rom_file_count(void) { return initialized ? (int)file_count : 0; }
int khdays_rom_file_info(int id, char *path, int size, u32 *offset, u32 *bytes)
{
    if (!initialized || id < 0 || (u32)id >= file_count || path == NULL || size <= 0 ||
        offset == NULL || bytes == NULL || strlen(files[id].name) + 1u > (size_t)size) {
        return 0;
    }
    memcpy(path, files[id].name, strlen(files[id].name) + 1u);
    *offset = le32(fat + 8u * (u32)id);
    *bytes = files[id].size;
    return 1;
}
int khdays_rom_file_at(u32 offset, char *path, int size, u32 *within)
{
    const size_t next = after(offset);
    const struct Region *r = next != 0 ? &regions[next - 1u] : NULL;
    if (!initialized || r == NULL || offset >= r->end || r->kind != GAME_FILE ||
        path == NULL || size <= 0 || within == NULL || strlen(files[r->index].name) + 1u > (size_t)size) {
        return 0;
    }
    memcpy(path, files[r->index].name, strlen(files[r->index].name) + 1u);
    *within = offset - r->start;
    return 1;
}
int khdays_rom_area_at(u32 offset, char *name, int size, u32 *start, u32 *end)
{
    static const char *const system[] = {
        "(the ARM9 binary)", "(the ARM7 binary)", "(the overlay table)", "(the FNT)"
    };
    const size_t next = after(offset);
    const struct Region *r = next != 0 ? &regions[next - 1u] : NULL;
    const char *what;
    if (!initialized || r == NULL || offset >= r->end || name == NULL || size <= 0 ||
        start == NULL || end == NULL) {
        return 0;
    }
    switch (r->kind) {
    case HEADER: what = "(the header)"; break;
    case SYSTEM_BLOB: what = system[r->index]; break;
    case FAT: what = "(the FAT)"; break;
    case GAME_FILE: what = files[r->index].name; break;
    default: return 0;
    }
    if (strlen(what) + 1u > (size_t)size) {
        return 0;
    }
    memcpy(name, what, strlen(what) + 1u);
    *start = r->start;
    *end = r->end;
    return 1;
}

/* Nintendo LZ10/LZ11, with the same extended length and overlapping copy
 * semantics as assets/src/message.cpp, validating every source reference. */
static int unpack(const u8 *input, u32 bytes, u8 **output, u32 *size)
{
    u32 at = 4, count, made = 0;
    u8 *out;
    if (bytes < 4 || (input[0] != 0x10 && input[0] != 0x11)) {
        return 0;
    }
    count = (u32)input[1] | (u32)input[2] << 8 | (u32)input[3] << 16;
    if (count == 0) {
        if (bytes < 8) { return 0; }
        count = le32(input + 4);
        at = 8;
    }
    if (count > MAX_UNPACKED || (out = malloc(count ? count : 1u)) == NULL) {
        return 0;
    }
    while (made < count) {
        u8 flags;
        if (at >= bytes) { goto invalid; }
        flags = input[at++];
        for (unsigned bit = 0; bit < 8 && made < count; ++bit) {
            u32 length, distance;
            if ((flags & (0x80u >> bit)) == 0) {
                if (at >= bytes) { goto invalid; }
                out[made++] = input[at++];
                continue;
            }
            if (bytes - at < 2) { goto invalid; }
            const u8 b0 = input[at], b1 = input[at + 1u];
            const unsigned kind = b0 >> 4;
            if (input[0] == 0x10) {
                length = kind + 3u;
                distance = (((u32)b0 & 15u) << 8 | b1) + 1u;
                at += 2;
            } else if (kind >= 2) {
                length = kind + 1u;
                distance = (((u32)b0 & 15u) << 8 | b1) + 1u;
                at += 2;
            } else if (kind == 0) {
                if (bytes - at < 3) { goto invalid; }
                length = ((u32)b0 << 4 | b1 >> 4) + 0x11u;
                distance = (((u32)b1 & 15u) << 8 | input[at + 2u]) + 1u;
                at += 3;
            } else {
                if (bytes - at < 4) { goto invalid; }
                length = (((u32)b0 & 15u) << 12 | (u32)b1 << 4 | input[at + 2u] >> 4) + 0x111u;
                distance = (((u32)input[at + 2u] & 15u) << 8 | input[at + 3u]) + 1u;
                at += 4;
            }
            if (distance > made) { goto invalid; }
            if (length > count - made) { length = count - made; }
            for (u32 i = 0; i < length; ++i, ++made) {
                out[made] = out[made - distance];
            }
        }
    }
    *output = out;
    *size = count;
    return 1;
invalid:
    free(out);
    return 0;
}

int khdays_rom_file_data(KhdaysRomReader *reader, const char *path, int entry, u8 **data, u32 *size)
{
    u32 top, bytes, within = 0, length;
    int packed;
    u8 *raw;
    if (data == NULL || size == NULL) {
        return 0;
    }
    *data = NULL;
    *size = 0;
    if (reader == NULL || !khdays_rom_find(path, &top, &bytes)) {
        return 0;
    }
    length = bytes;
    const size_t path_size = strlen(path);
    packed = path_size > 2 && strcmp(path + path_size - 2u, ".z") == 0;
    if (entry >= 0) {
        u8 head[0x10], at[2], raw_length[4];
        u32 count, length_at;
        if (bytes < sizeof(head)) { return 0; }
        read_rom(reader, top, head, sizeof(head));
        count = le16(head + 2) & 0x1ffu;
        length_at = 0x10u + (count + 1u) / 2u * 4u + 4u * (u32)entry;
        if (head[0] != 'P' || head[1] != '2' || (u32)entry >= count ||
            0x10u + 2u * (u32)entry + 2u > bytes || length_at > bytes || bytes - length_at < 4) {
            return 0;
        }
        read_rom(reader, top + 0x10u + 2u * (u32)entry, at, sizeof(at));
        read_rom(reader, top + length_at, raw_length, sizeof(raw_length));
        const u32 first = le32(head + 0xc), aligned = (u32)le16(at) << 9;
        if (first > bytes || aligned > bytes - first) { return 0; }
        within = first + aligned;
        length = le32(raw_length) & 0x7fffffffu;
        packed = (le32(raw_length) & 0x80000000u) != 0;
        if (length > bytes - within) { return 0; }
    }
    raw = malloc(length ? length : 1u);
    if (raw == NULL) { return 0; }
    read_rom(reader, top + within, raw, length);
    if (packed) {
        const int ok = unpack(raw, length, data, size);
        free(raw);
        return ok;
    }
    *data = raw;
    *size = length;
    return 1;
}
