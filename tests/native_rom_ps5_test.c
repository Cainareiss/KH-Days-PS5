/* Host validation of the production PS5 card reader. The Windows harness
 * selects an extraction directly; the PS5 object also compiles and uses the
 * POSIX hash/mod directory scanner. No synthetic game data is used here. */
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS
#define KHDAYS_ROM_NO_DIRECTORY_SCAN
#if defined(_WIN32)
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif
#include "../native/ps5/rom_ps5.c"

static int failures;

void khdays_host_tell_no_data(const char *where, const char *what)
{
    fprintf(stderr, "host data report: %s: %s\n", where, what ? what : "absent");
}
void khdays_origin_want(void) {}

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static void check_lz(void)
{
    static const u8 literal[] = {0x10, 3, 0, 0, 0, 'a', 'b', 'c'};
    static const u8 lz10[] = {0x10, 6, 0, 0, 0x10, 'a', 'b', 'c', 0x00, 0x02};
    static const u8 lz11[] = {0x11, 6, 0, 0, 0x10, 'a', 'b', 'c', 0x20, 0x02};
    static const u8 overlap[] = {0x10, 7, 0, 0, 0x40, 'x', 0x30, 0};
    static const u8 bad[] = {0x11, 6, 0, 0, 0x80, 0x20, 0x00};
    static const u8 extended[] = {0x11, 0, 0, 0, 3, 0, 0, 0, 0, 'a', 'b', 'c'};
    static const u8 medium[] = {0x11, 18, 0, 0, 0x40, 'x', 0, 0, 0};
    static const u8 long_run[] = {0x11, 0x12, 1, 0, 0x40, 'x', 0x10, 0, 0, 0};
    const struct { const u8 *source; u32 bytes; const char *expected; u32 size; } cases[] = {
        {literal, sizeof(literal), "abc", 3}, {lz10, sizeof(lz10), "abcabc", 6},
        {lz11, sizeof(lz11), "abcabc", 6}, {overlap, sizeof(overlap), "xxxxxxx", 7},
        {extended, sizeof(extended), "abc", 3}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        u8 *out = NULL;
        u32 size = 0;
        const int ok = unpack(cases[i].source, cases[i].bytes, &out, &size);
        check(ok && size == cases[i].size && memcmp(out, cases[i].expected, size) == 0,
              "LZ10/LZ11 fixture");
        free(out);
    }
    u8 *out = NULL;
    u32 size = 0;
    check(!unpack(bad, sizeof(bad), &out, &size), "reject LZ reference before output");
    check(!unpack(lz11, sizeof(lz11) - 1u, &out, &size), "reject truncated LZ stream");
    const struct { const u8 *source; u32 bytes, size; } runs[] = {
        {medium, sizeof(medium), 18}, {long_run, sizeof(long_run), 274}
    };
    for (size_t i = 0; i < sizeof(runs) / sizeof(runs[0]); ++i) {
        out = NULL;
        size = 0;
        const int ok = unpack(runs[i].source, runs[i].bytes, &out, &size);
        check(ok && size == runs[i].size, "LZ11 medium/long length forms");
        for (u32 at = 0; ok && at < size; ++at) {
            check(out[at] == 'x', "LZ11 long overlapping copy");
        }
        free(out);
    }
}

int main(int argc, char **argv)
{
    u8 card[8192], original[8192];
    u32 offset, size;
    unsigned long long compared = 0;
    FILE *rom;
    KhdaysRomReader *reader;
    if (argc != 3) {
        fprintf(stderr, "usage: native_rom_ps5_test EXTRACTED_HASH_DIRECTORY ORIGINAL.nds\n");
        return 2;
    }
    check_lz();
    check(!khdays_ps5_set_paths("relative", NULL, NULL), "reject relative data root");
    check(strcmp(khdays_ps5_save_root(), "/data/khdays-native/saves") == 0, "writable save default");
    if (!khdays_ps5_set_paths(argv[1], NULL, NULL) || !khdays_rom_init()) {
        return 1;
    }
    rom = fopen(argv[2], "rb");
    if (rom == NULL) {
        fprintf(stderr, "cannot open original dump\n");
        return 1;
    }
    reader = khdays_rom_reader_new();
    check(reader != NULL, "independent sound reader");
    if (reader == NULL) { fclose(rom); return 1; }
    check(khdays_rom_file_count() == 1898, "real YKGP FAT contains 1898 files");
    for (int id = 0; id < khdays_rom_file_count(); ++id) {
        char path[1024], found[1024];
        u32 bytes, top, within;
        if (!khdays_rom_file_info(id, path, sizeof(path), &offset, &size)) {
            check(0, "FAT id has a path");
            break;
        }
        check(khdays_rom_find(path, &top, &bytes) && top == offset && bytes == size,
              "FNT lookup agrees with FAT");
        if (size != 0) {
            check(khdays_rom_file_at(offset, found, sizeof(found), &within) &&
                      within == 0 && strcmp(path, found) == 0, "ROM address maps to same file");
        }
        check(fseek(rom, (long)offset, SEEK_SET) == 0, "seek original dump");
        for (u32 at = 0; at < size;) {
            const u32 chunk = size - at < sizeof(card) ? size - at : sizeof(card);
            khdays_rom_reader_read(reader, offset + at, card, chunk);
            check(fread(original, 1, chunk, rom) == chunk && memcmp(card, original, chunk) == 0,
                  "streamed NitroFS bytes match the original cartridge");
            at += chunk;
            compared += chunk;
        }
    }
    for (size_t i = 0; i < region_count; ++i) {
        const struct Region *r = &regions[i];
        if (r->kind == GAME_FILE || r->kind == UNEXTRACTED) { continue; }
        check(fseek(rom, (long)r->start, SEEK_SET) == 0, "seek original system blob");
        for (u32 at = r->start; at < r->end;) {
            const u32 chunk = r->end - at < sizeof(card) ? r->end - at : sizeof(card);
            khdays_rom_read(at, card, chunk);
            check(fread(original, 1, chunk, rom) == chunk && memcmp(card, original, chunk) == 0,
                  "streamed system bytes match original cartridge");
            at += chunk;
            compared += chunk;
        }
    }
    check(!khdays_rom_find("/../escape", &offset, &size), "path traversal is not a game file");
    check(!khdays_rom_file_info(-1, (char *)card, sizeof(card), &offset, &size), "negative FAT id rejected");
    check(!khdays_mods_find("../escape", (char *)card, sizeof(card)), "path traversal rejected in mods");
    check(khdays_rom_find("/db/db_en.p2", &offset, &size), "message archive found by root path");
    u8 *message = NULL;
    u32 message_size = 0;
    check(khdays_rom_file_data(reader, "/db/db_en.p2", 0, &message, &message_size) && message_size != 0,
          "real P2 message entry decodes");
    free(message);
    khdays_ps5_rom_reader_delete(reader);
    fclose(rom);
    discard();
    printf("ROM validation: %llu bytes compared, %d failures\n", compared, failures);
    return failures != 0;
}
