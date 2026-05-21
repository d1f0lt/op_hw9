#include "ext2common.h"

static struct ext2_fs fs;
static uint64_t remaining;
static char *zero_block;

static void write_all(const void *buf, size_t n) {
    size_t w = 0;
    while (w < n) {
        ssize_t r = write(STDOUT_FILENO, (const char *)buf + w, n - w);
        if (r < 0) { if (errno == EINTR) continue; die("write"); }
        w += (size_t)r;
    }
}

static void emit_block(uint32_t block) {
    if (remaining == 0) return;
    static char *buf = NULL;
    if (!buf) { buf = malloc(fs.block_size); if (!buf) die("malloc"); }
    if (block == 0) {
        size_t n = fs.block_size;
        if (n > remaining) n = (size_t)remaining;
        write_all(zero_block, n);
        remaining -= n;
        return;
    }
    ext2_read_block(&fs, block, buf);
    size_t n = fs.block_size;
    if (n > remaining) n = (size_t)remaining;
    write_all(buf, n);
    remaining -= n;
}

static void walk_indirect(uint32_t block, int level) {
    if (remaining == 0) return;
    uint32_t per = fs.block_size / 4;
    if (block == 0) {
        uint64_t span = 1;
        for (int i = 0; i < level; i++) span *= per;
        uint64_t bytes = span * fs.block_size;
        if (bytes > remaining) bytes = remaining;
        while (bytes && remaining) {
            size_t n = fs.block_size;
            if (n > bytes) n = (size_t)bytes;
            if (n > remaining) n = (size_t)remaining;
            write_all(zero_block, n);
            bytes -= n;
            remaining -= n;
        }
        return;
    }
    uint32_t *buf = malloc(fs.block_size);
    if (!buf) die("malloc");
    ext2_read_block(&fs, block, buf);
    for (uint32_t i = 0; i < per && remaining; i++) {
        uint32_t b = le32toh(buf[i]);
        if (level == 1) emit_block(b);
        else walk_indirect(b, level - 1);
    }
    free(buf);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <image> <inode>\n", argv[0]);
        return 1;
    }
    ext2_open(&fs, argv[1]);

    unsigned long ino = strtoul(argv[2], NULL, 0);
    struct ext2_inode inode;
    ext2_read_inode(&fs, (uint32_t)ino, &inode);

    uint16_t mode = le16toh(inode.i_mode);
    uint64_t size = le32toh(inode.i_size);
    if ((mode & EXT2_S_IFMT) == EXT2_S_IFREG)
        size |= ((uint64_t)le32toh(inode.i_dir_acl)) << 32;
    remaining = size;

    if ((mode & EXT2_S_IFMT) == EXT2_S_IFLNK && size <= 60 && le32toh(inode.i_blocks) == 0) {
        write_all(inode.i_block, (size_t)size);
        close(fs.fd);
        return 0;
    }

    zero_block = calloc(1, fs.block_size);
    if (!zero_block) die("calloc");

    for (int i = 0; i < EXT2_NDIR_BLOCKS && remaining; i++)
        emit_block(le32toh(inode.i_block[i]));
    if (remaining) walk_indirect(le32toh(inode.i_block[EXT2_IND_BLOCK]),  1);
    if (remaining) walk_indirect(le32toh(inode.i_block[EXT2_DIND_BLOCK]), 2);
    if (remaining) walk_indirect(le32toh(inode.i_block[EXT2_TIND_BLOCK]), 3);

    free(zero_block);
    close(fs.fd);
    return 0;
}
