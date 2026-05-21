#ifndef EXT2COMMON_H
#define EXT2COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "ext2fs.h"

static inline void die(const char *msg) {
    if (errno) perror(msg); else fprintf(stderr, "%s\n", msg);
    exit(1);
}

static inline void pread_exact(int fd, void *buf, size_t len, off_t off) {
    size_t got = 0;
    while (got < len) {
        ssize_t r = pread(fd, (char *)buf + got, len - got, off + (off_t)got);
        if (r < 0) { if (errno == EINTR) continue; die("pread"); }
        if (r == 0) { fprintf(stderr, "short read\n"); exit(1); }
        got += (size_t)r;
    }
}

struct ext2_fs {
    int fd;
    uint32_t block_size;
    uint32_t inodes_per_group;
    uint32_t inodes_count;
    uint32_t inode_size;
    uint32_t first_data_block;
};

static inline void ext2_open(struct ext2_fs *fs, const char *path) {
    fs->fd = open(path, O_RDONLY);
    if (fs->fd < 0) die("open");
    struct ext2_superblock sb;
    pread_exact(fs->fd, &sb, sizeof(sb), EXT2_SUPERBLOCK_OFFSET);
    if (le16toh(sb.s_magic) != EXT2_SUPER_MAGIC) {
        fprintf(stderr, "bad ext2 magic\n"); exit(1);
    }
    fs->block_size = 1024u << le32toh(sb.s_log_block_size);
    fs->inodes_per_group = le32toh(sb.s_inodes_per_group);
    fs->inodes_count = le32toh(sb.s_inodes_count);
    fs->first_data_block = le32toh(sb.s_first_data_block);
    fs->inode_size = (le32toh(sb.s_rev_level) >= 1)
        ? le16toh(sb.s_inode_size) : EXT2_GOOD_OLD_INODE_SIZE;
}

static inline void ext2_read_inode(struct ext2_fs *fs, uint32_t ino, struct ext2_inode *out) {
    if (ino == 0 || ino > fs->inodes_count) {
        fprintf(stderr, "inode %u out of range\n", ino); exit(1);
    }
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    uint32_t gdt_block = fs->first_data_block + 1;
    uint32_t gd_per_block = fs->block_size / sizeof(struct ext2_group_desc);
    uint32_t gd_block = gdt_block + group / gd_per_block;
    uint32_t gd_idx   = group % gd_per_block;
    off_t gd_off = (off_t)gd_block * fs->block_size + (off_t)gd_idx * sizeof(struct ext2_group_desc);
    struct ext2_group_desc gd;
    pread_exact(fs->fd, &gd, sizeof(gd), gd_off);
    uint32_t inode_table = le32toh(gd.bg_inode_table);
    off_t ino_off = (off_t)inode_table * fs->block_size + (off_t)index * fs->inode_size;
    pread_exact(fs->fd, out, sizeof(*out), ino_off);
}

static inline void ext2_read_block(struct ext2_fs *fs, uint32_t block, void *buf) {
    if (block == 0) { memset(buf, 0, fs->block_size); return; }
    pread_exact(fs->fd, buf, fs->block_size, (off_t)block * fs->block_size);
}

#endif
