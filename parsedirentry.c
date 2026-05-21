#include "ext2common.h"

int main(int argc, char **argv) {
    int fd = STDIN_FILENO;
    if (argc == 2) { fd = open(argv[1], O_RDONLY); if (fd < 0) die("open"); }

    size_t len = 0, cap = 262144;
    unsigned char *buf = malloc(cap);
    if (!buf) die("malloc");
    for (;;) {
        ssize_t r = read(fd, buf + len, cap - len);
        if (r < 0) { if (errno == EINTR) continue; die("read"); }
        if (r == 0) break;
        len += (size_t)r;
        if (len == cap) { fprintf(stderr, "buffer full\n"); exit(1); }
    }
    if (fd != STDIN_FILENO) close(fd);

    printf("%-10s %s\n", "INODE", "NAME");

    size_t off = 0;
    while (off + 8 <= len) {
        struct ext2_dir_entry e;
        memcpy(&e, buf + off, 8);
        uint32_t ino     = le32toh(e.inode);
        uint16_t rec_len = le16toh(e.rec_len);
        uint8_t  nlen    = e.name_len;

        if (rec_len < 8 || off + rec_len > len) break;
        if (ino != 0 && nlen > 0 && off + 8 + nlen <= len)
            printf("%-10u %.*s\n", ino, (int)nlen, (const char *)(buf + off + 8));
        off += rec_len;
    }
    free(buf);
    return 0;
}
