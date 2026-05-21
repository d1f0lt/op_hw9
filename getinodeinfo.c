#include "ext2common.h"
#include <time.h>

const char *type_str(uint16_t mode) {
    switch (mode & EXT2_S_IFMT) {
        case EXT2_S_IFREG:  return "regular";
        case EXT2_S_IFDIR:  return "directory";
        case EXT2_S_IFLNK:  return "symlink";
        case EXT2_S_IFBLK:  return "block dev";
        case EXT2_S_IFCHR:  return "char dev";
        case EXT2_S_IFIFO:  return "fifo";
        case EXT2_S_IFSOCK: return "socket";
        default: return "unknown";
    }
}

void print_indirect(struct ext2_fs *fs, uint32_t block, int level, const char *label) {
    printf("  %s: %u\n", label, block);
    if (block == 0) return;
    uint32_t per = fs->block_size / 4;
    uint32_t *buf = malloc(fs->block_size);
    if (!buf) die("malloc");
    ext2_read_block(fs, block, buf);
    if (level == 1) {
        printf("    data:");
        for (uint32_t i = 0; i < per; i++) {
            uint32_t b = le32toh(buf[i]);
            if (b) printf(" %u", b);
        }
        printf("\n");
    } else {
        for (uint32_t i = 0; i < per; i++) {
            uint32_t b = le32toh(buf[i]);
            if (b) print_indirect(fs, b, level - 1, label);
        }
    }
    free(buf);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <image> <inode>\n", argv[0]);
        return 1;
    }
    struct ext2_fs fs;
    ext2_open(&fs, argv[1]);

    unsigned long ino = strtoul(argv[2], NULL, 0);
    struct ext2_inode inode;
    ext2_read_inode(&fs, (uint32_t)ino, &inode);

    uint16_t mode = le16toh(inode.i_mode);
    uint64_t size = le32toh(inode.i_size);
    if ((mode & EXT2_S_IFMT) == EXT2_S_IFREG)
        size |= ((uint64_t)le32toh(inode.i_dir_acl)) << 32;

    char ta[32], tm[32], tc[32];
    time_t t;
    struct tm tmb;
    t = le32toh(inode.i_atime); gmtime_r(&t, &tmb); strftime(ta, sizeof(ta), "%F %T", &tmb);
    t = le32toh(inode.i_mtime); gmtime_r(&t, &tmb); strftime(tm, sizeof(tm), "%F %T", &tmb);
    t = le32toh(inode.i_ctime); gmtime_r(&t, &tmb); strftime(tc, sizeof(tc), "%F %T", &tmb);

    printf("Inode: %lu\n", ino);
    printf("  Type:   %s\n", type_str(mode));
    printf("  Mode:   0%o\n", mode & 0xFFF);
    printf("  UID:    %u\n", le16toh(inode.i_uid));
    printf("  GID:    %u\n", le16toh(inode.i_gid));
    printf("  Size:   %llu\n", (unsigned long long)size);
    printf("  Links:  %u\n", le16toh(inode.i_links_count));
    printf("  Blocks: %u\n", le32toh(inode.i_blocks));
    printf("  Flags:  0x%x\n", le32toh(inode.i_flags));
    printf("  atime:  %s\n", ta);
    printf("  mtime:  %s\n", tm);
    printf("  ctime:  %s\n", tc);

    printf("Blocks:\n");
    printf("  Direct:");
    for (int i = 0; i < EXT2_NDIR_BLOCKS; i++)
        printf(" %u", le32toh(inode.i_block[i]));
    printf("\n");
    print_indirect(&fs, le32toh(inode.i_block[EXT2_IND_BLOCK]),  1, "Indirect");
    print_indirect(&fs, le32toh(inode.i_block[EXT2_DIND_BLOCK]), 2, "Double-indirect");
    print_indirect(&fs, le32toh(inode.i_block[EXT2_TIND_BLOCK]), 3, "Triple-indirect");

    close(fs.fd);
}
