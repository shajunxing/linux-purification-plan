/*
raw.c - fat16 super floppy raw image creator
Copyright 2024-2026 ShaJunXing <shajunxing@hotmail.com>

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#ifdef _MSC_VER
    #define _CRT_SECURE_NO_WARNINGS
    #pragma warning(disable : 4996)
#endif
#include <ctype.h> /* isalnum */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h> /* offsetof */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h> /* strerror */

#define BYTES_PER_SECTOR 512 /* 通常为 512 */
#define MIN_SECTORS_PER_CLUSTER 1 /* 必须是 2 的幂 注意：FAT16 的标准兼容性要求通常将最大簇大小限制为 32 KiB */
#define MAX_SECTORS_PER_CLUSTER 64
/*
注意，依照微软判定标准：
| 数据簇数量 | 文件系统类型 |
| ---------- | ------ |
| 少于 4085 | FAT12 |
| 4085～65524 | FAT16 |
| 65525 及以上 | FAT32 |
SECTORS_PER_CLUSTER取最小值1，那么容量大概2MB~32MB；取最大值64，容量大概128MB~2GB
*/
#define MIN_CLUSTERS 4085
#define MAX_CLUSTERS 65524
#define RESERVED_SECTORS 1 /* 只预留引导扇区 */
#define FAT_COUNT 2
#define SECTORS_PER_FAT 256 /* 可容纳最大数量 */
#define FAT_SIZE (SECTORS_PER_FAT * BYTES_PER_SECTOR / 2)
#define ROOT_SECTORS 32 /* 为了兼容性，常见 FAT16 格式化工具通常将根目录项数设为 512 */
#define ROOT_ENTRY_SIZE (ROOT_SECTORS * BYTES_PER_SECTOR / sizeof(struct dir_entry))
#define DATA_REGION_STARTING_SECTOR (RESERVED_SECTORS + SECTORS_PER_FAT * FAT_COUNT + ROOT_SECTORS)

#define trace(__arg_format, ...) \
    printf("%s:%d:%s: " __arg_format "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define trace_expr(__arg_format, ...) \
    trace(#__VA_ARGS__ " = " __arg_format, ##__VA_ARGS__)

#define posix_die_0() \
    do { \
        trace("%d: %s", errno, strerror(errno)); \
        exit(EXIT_FAILURE); \
    } while (0)
#define posix_die_1(__arg_n) \
    do { \
        errno = __arg_n; \
        posix_die_0(); \
    } while (0)
#define get_posix_die_macro(_0, _1, name, ...) name
#define posix_die(...) get_posix_die_macro(_, ##__VA_ARGS__, posix_die_1, posix_die_0)(__VA_ARGS__)

/*
https://wiki.osdev.org/FAT
*/
#pragma pack(push, 1)
struct dir_entry {
    char name[8];
    char ext[3];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t first_cluster_hi;
    uint16_t last_modified_time;
    uint16_t last_modified_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
};
static_assert(sizeof(struct dir_entry) == 32);
struct boot_sector {
    uint8_t jmp_boot[3];
    char oem_name[8];
    /* bpb */
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entry_size;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* extended bpb */
    uint8_t drive_number;
    uint8_t reserved;
    uint8_t boot_signature;
    uint32_t volume_id;
    char volume_label[11];
    char fs_type_label[8];
    /* 此处要根据boot.bin的长度小心定义 */
#define BOOT_BIN_LEN 370
    uint8_t bootstrap[BOOT_BIN_LEN];
    uint32_t current_lba; /* for bootstrap asm code to load vmlinuz */
    char cmd_line[448 - BOOT_BIN_LEN - sizeof(uint32_t)];
#undef BOOT_BIN_LEN
    uint8_t signature[2];
};
static_assert(sizeof(struct boot_sector) == BYTES_PER_SECTOR);
static_assert(offsetof(struct boot_sector, volume_label) == 0x2B);
#pragma pack(pop)

/* 向“外”取整 */
static long ldiv_round_away(long numer, long denom) {
    ldiv_t ret = ldiv(numer, denom);
    return ret.quot + (ret.rem ? (ret.quot > 0 ? 1 : -1) : 0); /* 三目运算符优先级更低 */
}

static char *basename(char *path) {
    char *n1 = strrchr(path, '/');
#ifdef _WIN32 /* Windows：同时识别 / 和 \ */
    char *n2 = strrchr(path, '\\');
    if (!n1 || (n2 && n2 > n1)) n1 = n2;
#endif
    return n1 ? n1 + 1 : path;
}

/*
检查是否符合8.3短文件名允许的字符
https://www.isdaman.com/alsos/protocols/fats/fatgen103.pdf 第29页
完整的规范还需要支持代码页，不同代码页转换大写规则也不一样，toupper就是依照当前代码页来的，但bpb并没有代码页字段，而是系统约定的，所以这里为了避免潜在问题，只允许127以内的ascii字符
*/
static bool validate_chars(char *s, size_t len) {
    // printf("Validating: \"%.*s\"\n", (int)len, s);
    for (size_t i = 0; i < len; i++) {
        int c = (unsigned char)s[i];
        if (c > 127 || !isalnum(c) && strchr("$%'-_@~`!(){}^#&", c) == NULL) return false;
    }
    return true;
}

/* 写入字节按扇区对齐，不足的补零 */
static void write_bytes(FILE *out, size_t *pss, size_t ns, const void *buf, size_t len) {
    if (ns * BYTES_PER_SECTOR < len) posix_die(E2BIG);
    long offset = (long)(*pss * BYTES_PER_SECTOR);
    if (fseek(out, offset, SEEK_SET) != 0) posix_die();
    if (fwrite(buf, 1, len, out) != len) posix_die();
    for (size_t i = 0; i < ns * BYTES_PER_SECTOR - len; i++) {
        if (fputc('\0', out) == EOF) posix_die();
    }
    *pss += ns;
}

/*
Disk image format is fat16 \"super floppy\" (no partitions). All files will be sequentially written to root directory, continous sectors. Filenames are strictly limited to \"8.3\" format and will be converted to uppercase. First file will treated as bootable linux kernel.
*/
int main(int argc, char *argv[]) {
    char *image_filename = NULL;
#define NAME_LEN (sizeof((struct dir_entry *)0)->name)
#define EXT_LEN (sizeof((struct dir_entry *)0)->ext)
    struct {
        char *path;
        char name[NAME_LEN];
        char ext[EXT_LEN];
        long size;
    } *file_table = calloc(argc, sizeof(*file_table)); /* 始终以空记录结束 */
    if (!file_table) posix_die();
    int index = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) goto help;
        else if (strcmp(argv[i], "-o") == 0) {
            i++;
            if (i < argc) image_filename = argv[i];
            else {puts("Missing image filename"); goto help;}
        }
        else {
            char *path = argv[i];
            char *nodir = basename(path);
            size_t nodir_len = strlen(nodir);
            char *dot = strrchr(nodir, '.');
            char *name = nodir;
            size_t name_len = dot ? (size_t)(ptrdiff_t)(dot - nodir) : nodir_len;
            char *ext = dot ? dot + 1 : "";
            size_t ext_len = strlen(ext);
            if (name_len == 0 || !validate_chars(name, name_len) || !validate_chars(ext, ext_len)) {
                printf("Illegal filename \"%s\"\n", nodir);
                goto help;
            }
            if (name_len > NAME_LEN || ext_len > EXT_LEN) {
                printf("Too long filename \"%s\"\n", nodir);
                goto help;
            }
            /*
            TODO: 检查重名
            */
            typeof(file_table) pfile = file_table + index++;
            pfile->path = path;
            memset(pfile->name, ' ', sizeof(pfile->name)); /* fat规范用空格填充空白 */
            memset(pfile->ext, ' ', sizeof(pfile->ext));
            for (size_t i = 0; i < name_len; i++) pfile->name[i] = toupper(name[i]);
            for (size_t i = 0; i < ext_len; i++) pfile->ext[i] = toupper(ext[i]);
        }
    }
    if (!image_filename) {puts("Missing image filename"); goto help;}
    index = 0;
#define file_table_for_each(__arg_entry) for (typeof(file_table) __arg_entry = file_table; __arg_entry->path; __arg_entry++)
    /* 计算簇大小 */
    long total_file_size = 0;
    file_table_for_each(pfile) {
        FILE *in = fopen(pfile->path, "rb");
        if (!in) posix_die();
        if (fseek(in, 0, SEEK_END) != 0) posix_die();
        long size = ftell(in);
        if (size < 0) posix_die();
        pfile->size = size;
        total_file_size += size;
        fclose(in);
        // printf("%d: %.*s %.*s %ld\n", index++, (int)NAME_LEN, pfile->name, (int)EXT_LEN, pfile->ext, pfile->size);
    }
    long sectors_per_cluster = 1, bytes_per_cluster = 0;
    for (;;) {
        if (sectors_per_cluster > MAX_SECTORS_PER_CLUSTER) posix_die(EFBIG);
        bytes_per_cluster = sectors_per_cluster * BYTES_PER_SECTOR;
        long clusters_required = 0;
        file_table_for_each(pfile) {
            clusters_required += ldiv_round_away(pfile->size, bytes_per_cluster);
        }
        if (clusters_required <= MAX_CLUSTERS) {
            printf("Sectors per cluster: %ld\n", sectors_per_cluster);
            printf("Clusters required: %ld\n", clusters_required);
            break;
        }
        sectors_per_cluster <<= 1;
    }
    /* 构建镜像 */
    struct boot_sector boot_sector = {
        .jmp_boot = {0xEB, 0x3C, 0x90},
        .bytes_per_sector = BYTES_PER_SECTOR,
        .sectors_per_cluster = (uint8_t)sectors_per_cluster,
        .reserved_sectors = RESERVED_SECTORS,
        .fat_count = FAT_COUNT,
        .root_entry_size = ROOT_ENTRY_SIZE,
        .media_type = 0xF8,
        .sectors_per_fat = SECTORS_PER_FAT,
        .sectors_per_track = 63, /* 为了mtools正常读取 */
        .head_count = 255, /* 为了mtools正常读取 */
        /*
        必须加上，否则 fsck.fat 报错：
        Volume label '0123456789A' stored in root directory and label '' stored in boot sector and different.
        */
        .boot_signature = 0x29,
        /* 用字符串字面量初始化字符数组时，如果数组刚好容纳字符串内容，可以省略末尾的 '\0' */
        .volume_label = "0123456789A",
        .fs_type_label = "FAT16   ", /* FAT 固定字段的填充字符是空格 0x20，不是 '\0' */
        /*
        根据 C 标准， int a[2] = {1,2,3}; 属于 constraint violation（约束违反），编译器必须给出诊断信息（diagnostic）。编译器可以在报错后继续作为扩展处理，但标准不保证它会自动截断。但字符串字面量初始化字符数组是一个特殊情况， char s[2] = "abc"; 是合法的 C 代码，数组只保留前两个字符，注意：不会自动添加 '\0' 终止符。
        */
        .bootstrap = {
#include "../bin/boot.inc"
        },
        .current_lba = DATA_REGION_STARTING_SECTOR, /* for bootstrap asm code to load vmlinuz */
        .cmd_line = "root=/dev/sda rootfstype=msdos init=/init clocksource=tsc tsc=reliable",
        .signature = {0x55, 0xAA},
    };
    /*
    | FAT16 表项值         | 含义                     |
    | ----------------- | ---------------------- |
    | `0x0000`          | 空闲簇                    |
    | `0x0001`          | 保留值                    |
    | `0x0002`～`0xFFEF` | 通常是下一个簇号，但还要确认簇号在有效范围内 |
    | `0xFFF0`～`0xFFF6` | 保留值                    |
    | `0xFFF7`          | 坏簇                     |
    | `0xFFF8`～`0xFFFF` | 文件簇链结束（EOC）            |
    */
    uint16_t fat_table[FAT_SIZE] = {0xFFF8, 0xFFFF}; /* F8对应media_type */
    uint32_t next_cluster = 2;
#define num_clusters() (next_cluster - 2)
    /*
    FAT 规范中：
    - 如果根目录有卷标项，那么它应该是主卷标；
    - boot sector 中的 label 是历史兼容字段。
    如果二者不一致，fsck.fat 默认认为文件系统状态不一致：
        Label '' stored in boot sector is not valid.
          Auto-removing label from boot sector.
    */
    struct dir_entry root_table[ROOT_ENTRY_SIZE] = {
        {
            .name = "01234567",
            .ext = "89A",
            .attr = 0x08,
        },
    };
    uint16_t next_root = 1;
    FILE *out = fopen(image_filename, "wb");
    if (!out) posix_die();
    /*
    先写数据区，确保把镜像末尾扩展到正确位置
    写入文件按簇对齐，不足的补零
    */
    size_t buflen = bytes_per_cluster;
    char *buf = calloc(buflen, 1);
    if (!buf) posix_die();
    puts("Name     Ext       Size  From");
    puts("-----------------------------");
    file_table_for_each(pfile) {
        FILE *in = fopen(pfile->path, "rb");
        if (!in) posix_die();
        long offset = DATA_REGION_STARTING_SECTOR * BYTES_PER_SECTOR + num_clusters() * bytes_per_cluster;
        if (fseek(out, offset, SEEK_SET) != 0) posix_die();
        if (next_root >= ROOT_ENTRY_SIZE) posix_die(ENOSPC);
        struct dir_entry *pdir = root_table + (next_root++);
        memcpy(pdir->name, pfile->name, NAME_LEN);
        memcpy(pdir->ext, pfile->ext, EXT_LEN);
        /*
        先得到文件大小，避免大小是 0 或簇大小的整数倍时，分配簇的逻辑混乱
        */
        pdir->file_size = (uint32_t)pfile->size;
        if (pdir->file_size == 0) {
            pdir->first_cluster_lo = 0;
        } else {
            long count = ldiv_round_away(pfile->size, bytes_per_cluster);
            if (count + next_cluster > FAT_SIZE || count + next_cluster > 0xFFF0u) posix_die(ENOSPC);
            pdir->first_cluster_lo = (uint16_t)next_cluster;
            for (long i = 0; i < count; i++) {
                memset(buf, 0, buflen);
                size_t n = fread(buf, 1, buflen, in);
                if (ferror(in)) posix_die();
                long expected = pfile->size - i * bytes_per_cluster;
                if (expected > bytes_per_cluster) {
                    expected = bytes_per_cluster;
                }
                if (n != expected) posix_die(EIO);
                if (fwrite(buf, 1, buflen, out) != buflen) posix_die();
                fat_table[next_cluster] =
                    (i + 1 == count) ? 0xFFFF : (uint16_t)(next_cluster + 1);
                next_cluster++;
            }
        }
        printf("%.*s %.*s %10ld %5u\n", (int)NAME_LEN, pdir->name, (int)EXT_LEN, pdir->ext, pdir->file_size, pdir->first_cluster_lo);
        pdir->attr = 0x20; /* Archive 属性 */
        // pdir->create_date = (46 << 9) | (10 << 5) | 13; /* 填入时间测试 */
        // pdir->last_access_date = pdir->create_date;
        // pdir->last_modified_date = pdir->create_date;
        // pdir->create_time = 12 << 11;
        // pdir->last_modified_time = 12 << 11;
        fclose(in);
    }
    free(buf);
    /* 补足 */
    if (num_clusters() < MIN_CLUSTERS) {
        if (fseek(out, 0, SEEK_END) != 0) posix_die();
        for (uint32_t i = num_clusters(); i < MIN_CLUSTERS; i++) {
            for (int j = 0; j < bytes_per_cluster; j++) {
                if (fputc('\0', out) == EOF) posix_die();
            }
        }
        boot_sector.total_sectors_32 = DATA_REGION_STARTING_SECTOR + MIN_CLUSTERS * sectors_per_cluster;
    } else {
        boot_sector.total_sectors_32 = DATA_REGION_STARTING_SECTOR + num_clusters() * sectors_per_cluster;
    }
    /*
    再回到前面写其他区域
    */
    size_t sec = 0;
    write_bytes(out, &sec, RESERVED_SECTORS, &boot_sector, sizeof boot_sector);
    for (int i = 0; i < FAT_COUNT; i++) {
        write_bytes(out, &sec, SECTORS_PER_FAT, fat_table, sizeof fat_table);
    }
    write_bytes(out, &sec, ROOT_SECTORS, root_table, sizeof root_table);
    fclose(out);
    free(file_table);
    return EXIT_SUCCESS;
help:
    printf("Usage: %s -o <image filename> <filename> [filename] ... [-h] [--help], filenames must strictly follow \"8.3\" format.\n", basename(argv[0]));
    return EXIT_FAILURE;
}