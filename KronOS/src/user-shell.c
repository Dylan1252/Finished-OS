#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

/* ============================ Syscall interface ============================ */

static inline void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("int $0x30" : : "a"(eax), "b"(ebx), "c"(ecx), "d"(edx) : "memory");
}

static int8_t fs_read(struct EXT2DriverRequest *r) {
    int32_t ret = 0;
    syscall(0, (uint32_t) r, (uint32_t) &ret, 0);
    return (int8_t) ret;
}
static int8_t fs_read_dir(struct EXT2DriverRequest *r) {
    int32_t ret = 0;
    syscall(1, (uint32_t) r, (uint32_t) &ret, 0);
    return (int8_t) ret;
}
static int8_t fs_write(struct EXT2DriverRequest *r) {
    int32_t ret = 0;
    syscall(2, (uint32_t) r, (uint32_t) &ret, 0);
    return (int8_t) ret;
}
static int8_t fs_delete(struct EXT2DriverRequest *r) {
    int32_t ret = 0;
    syscall(3, (uint32_t) r, (uint32_t) &ret, 0);
    return (int8_t) ret;
}
static int8_t fs_move(struct EXT2MoveRequest *r) {
    int32_t ret = 0;
    syscall(17, (uint32_t) r, (uint32_t) &ret, 0);
    return (int8_t) ret;
}
static int8_t fs_size(struct EXT2DriverRequest *r, uint32_t *size) {
    int32_t ret = 0;
    syscall(9, (uint32_t) r, (uint32_t) &ret, (uint32_t) size);
    return (int8_t) ret;
}
static int8_t fs_dir_size(uint32_t inode, uint32_t *size) {
    int32_t ret = 0;
    syscall(16, inode, (uint32_t) &ret, (uint32_t) size);
    return (int8_t) ret;
}
static void kb_getchar(char *c) { syscall(4, (uint32_t) c, 0, 0); }
static void tty_putchar(char c, uint32_t color) { syscall(5, (uint32_t) c, color, 0); }
static void tty_puts(const char *s, uint32_t len, uint32_t color) { syscall(6, (uint32_t) s, len, color); }
static void kb_activate(void) { syscall(7, 0, 0, 0); }
static void tty_clear(void) { syscall(8, 0, 0, 0); }

/* -- Chapter 4: process & clock syscalls -- */
struct ProcessInfo {
    uint32_t state;
    uint32_t pid;
    char     name[32];
} __attribute__((packed));

static int8_t proc_exec(struct EXT2DriverRequest *r) {
    int32_t ret = 0;
    syscall(11, (uint32_t) r, (uint32_t) &ret, 0);
    return (int8_t) ret;
}
static int8_t proc_kill(uint32_t pid) {
    int32_t ret = 0;
    syscall(12, pid, (uint32_t) &ret, 0);
    return (int8_t) ret;
}
static void proc_info(uint32_t index, struct ProcessInfo *info) {
    syscall(13, index, (uint32_t) info, 0);
}

/* ============================ Local string helpers ============================ */

static uint32_t ustrlen(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static void ustrcpy(char *d, const char *s) {
    while (*s) *d++ = *s++;
    *d = 0;
}

static int ustrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int) (uint8_t) *a - (int) (uint8_t) *b;
}

/* ============================ Output (screen or pipe) ============================ */

#define OUT_COLOR 0xF

/* Two bounded buffers allow pipelines with any number of stages. */
#define PIPE_BUF_SIZE 65536
static char     pipe_a[PIPE_BUF_SIZE];
static char     pipe_b[PIPE_BUF_SIZE];
static const char *pipe_input = NULL;
static uint32_t pipe_input_len = 0;
static char     *pipe_output = NULL;
static uint32_t pipe_output_len = 0;
static bool     pipe_mode = false;

static void emit(const char *s, uint32_t n) {
    if (pipe_mode) {
        for (uint32_t i = 0; i < n && pipe_output_len + 1 < PIPE_BUF_SIZE; i++)
            pipe_output[pipe_output_len++] = s[i];
        pipe_output[pipe_output_len] = 0;
    } else {
        tty_puts(s, n, OUT_COLOR);
    }
}

static void emit_str(const char *s) { emit(s, ustrlen(s)); }

static void emit_uint(uint32_t v) {
    char b[16];
    int k = 0;
    if (v == 0) b[k++] = '0';
    while (v > 0) { b[k++] = (char) ('0' + (v % 10)); v /= 10; }
    for (int i = k - 1; i >= 0; i--) emit(&b[i], 1);
}

/* ============================ EXT2 helpers ============================ */

#define DIR_BUF_SIZE 524288
static uint8_t dir_buf[DIR_BUF_SIZE];

static char *entry_name(struct EXT2DirectoryEntry *e) {
    return (char *) e + sizeof(struct EXT2DirectoryEntry);
}

static int8_t list_into(uint32_t inode, uint8_t *buf, uint32_t size) {
    uint32_t directory_size = 0;
    if (fs_dir_size(inode, &directory_size) != 0 || directory_size > size)
        return -1;
    memset(buf, 0, directory_size);
    struct EXT2DriverRequest req;
    req.buf           = buf;
    req.name          = ".";
    req.name_len      = 1;
    req.parent_inode  = inode;
    req.buffer_size   = directory_size;
    req.is_directory  = true;
    return fs_read_dir(&req);
}

/* Case-sensitive name comparison */
static bool name_eq(const char *a, uint32_t alen, const char *b) {
    uint32_t blen = ustrlen(b);
    if (alen != blen) return false;
    return memcmp(a, b, alen) == 0;
}

/* Find a child entry inside a directory inode */
static bool dir_lookup(uint32_t dir_inode, const char *name, uint32_t name_len,
                       uint32_t *child, uint8_t *ftype) {
    if (list_into(dir_inode, dir_buf, DIR_BUF_SIZE) != 0) return false;

    uint32_t offset = 0;
    while (offset < DIR_BUF_SIZE) {
        struct EXT2DirectoryEntry *e =
            (struct EXT2DirectoryEntry *) (dir_buf + offset);
        if (e->rec_len == 0) break;
        if (e->inode != 0 && (uint32_t) e->name_len == name_len &&
            memcmp(entry_name(e), name, name_len) == 0) {
            if (child) *child = e->inode;
            if (ftype) *ftype = e->file_type;
            return true;
        }
        offset += e->rec_len;
    }
    return false;
}

/* ============================ Path handling ============================ */

#define MAX_DEPTH 16
static char     cwd_parts[MAX_DEPTH][256];
static uint32_t cwd_depth = 0;
static uint32_t cwd_inode = 1;

static void cwd_path(char *out) {
    if (cwd_depth == 0) {
        out[0] = '/';
        out[1] = 0;
        return;
    }
    uint32_t k = 0;
    for (uint32_t i = 0; i < cwd_depth && k < 250; i++) {
        out[k++] = '/';
        for (uint32_t j = 0; cwd_parts[i][j] && k < 250; j++)
            out[k++] = cwd_parts[i][j];
    }
    out[k] = 0;
}

/* Resolve a (possibly relative) path into an inode */
static int resolve(const char *path, uint32_t *out_inode, uint8_t *out_type, bool set_cwd) {
    uint32_t inode;
    int depth;
    char parts[MAX_DEPTH][256];

    if (path[0] == '/') {
        inode = 1;
        depth = 0;
    } else {
        inode = cwd_inode;
        depth = (int) cwd_depth;
        for (int i = 0; i < depth; i++)
            ustrcpy(parts[i], cwd_parts[i]);
    }

    uint8_t ftype = EXT2_FT_DIR;
    const char *p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        char comp[256];
        uint32_t cl = 0;
        while (*p && *p != '/') {
            if (cl == 255) return 2;
            comp[cl++] = *p++;
        }
        comp[cl] = 0;

        if (ustrcmp(comp, ".") == 0) continue;

        if (ustrcmp(comp, "..") == 0) {
            uint32_t parent = inode;
            if (!dir_lookup(inode, "..", 2, &parent, NULL)) parent = inode;
            inode = parent;
            if (depth > 0) depth--;
            ftype = EXT2_FT_DIR;
            continue;
        }

        uint32_t child = 0;
        uint8_t  ty = 0;
        if (!dir_lookup(inode, comp, cl, &child, &ty)) return 2;
        if (depth >= MAX_DEPTH) return 2;
        ustrcpy(parts[depth++], comp);
        inode = child;
        ftype = ty;
    }

    if (out_inode) *out_inode = inode;
    if (out_type) *out_type = ftype;

    if (set_cwd) {
        cwd_depth = (uint32_t) depth;
        for (uint32_t i = 0; i < cwd_depth; i++)
            ustrcpy(cwd_parts[i], parts[i]);
        cwd_inode = inode;
    }
    return 0;
}

/* Split "a/b/c" into dirname "a/b" and basename "c" */
static void split_path(const char *path, char *dir_out, char *base_out) {
    int last = -1;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/') last = i;

    if (last < 0) {
        dir_out[0] = 0;
        ustrcpy(base_out, path);
    } else if (last == 0) {
        dir_out[0] = '/';
        dir_out[1] = 0;
        ustrcpy(base_out, path + 1);
    } else {
        int k = 0;
        for (int i = 0; i < last; i++) dir_out[k++] = path[i];
        dir_out[k] = 0;
        ustrcpy(base_out, path + last + 1);
    }
}

/* Resolve parent directory inode and base name for a target path */
static int resolve_parent(const char *path, uint32_t *parent_inode, char *base_out) {
    char tmp[256];
    uint32_t k = 0;
    while (path[k] && k < 255) { tmp[k] = path[k]; k++; }
    if (path[k] != 0) return 1;
    tmp[k] = 0;
    /* Strip trailing slashes (keep single '/') */
    while (k > 1 && tmp[k - 1] == '/') tmp[--k] = 0;

    char dir[256];
    split_path(tmp, dir, base_out);
    if (base_out[0] == 0) return 1;

    if (dir[0] == 0) {
        *parent_inode = cwd_inode;
        return 0;
    }
    return resolve(dir, parent_inode, NULL, false);
}

/* ============================ File buffer ============================ */

#define FILE_BUF_SIZE 65536
static uint8_t file_buf[FILE_BUF_SIZE];

/* ============================ Input ============================ */

static char getch(void) {
    char c = 0;
    kb_getchar(&c);
    return c;
}

static void read_line(char *buf, uint32_t max) {
    uint32_t len = 0;
    for (;;) {
        char c = getch();
        if (c == 0) continue;
        if (c == '\n') { tty_putchar('\n', OUT_COLOR); break; }
        if (c == '\b') {
            if (len > 0) { len--; tty_putchar('\b', OUT_COLOR); }
            continue;
        }
        if (c >= 32 && c < 127 && len < max - 1) {
            buf[len++] = c;
            tty_putchar(c, OUT_COLOR);
        }
    }
    buf[len] = 0;
}

/* ============================ Utilities ============================ */

static bool contains(const char *hay, uint32_t haylen, const char *needle) {
    uint32_t nlen = ustrlen(needle);
    if (nlen == 0) return true;
    if (nlen > haylen) return false;
    for (uint32_t i = 0; i + nlen <= haylen; i++) {
        uint32_t j = 0;
        while (j < nlen && hay[i + j] == needle[j]) j++;
        if (j == nlen) return true;
    }
    return false;
}

static void build_child(const char *parent, const char *name, uint32_t name_len, char *out) {
    uint32_t k = 0;
    if (ustrcmp(parent, "/") == 0) {
        out[k++] = '/';
    } else {
        for (uint32_t i = 0; parent[i] && k < 240; i++) out[k++] = parent[i];
        out[k++] = '/';
    }
    for (uint32_t i = 0; i < name_len && k < 250; i++) out[k++] = name[i];
    out[k] = 0;
}

/* ============================ Commands ============================ */

static void cmd_help(void) {
    emit_str("KronOS Shell - built-in commands:\n");
    emit_str("  help                 show this help\n");
    emit_str("  clear                clear the screen\n");
    emit_str("  echo <text>          print text\n");
    emit_str("  cd <dir>             change directory (use .. to go up)\n");
    emit_str("  ls [dir]             list directory contents\n");
    emit_str("  mkdir <dir>          create a directory\n");
    emit_str("  cat <file>           print file contents\n");
    emit_str("  cp <src> <dst>       copy a file\n");
    emit_str("  mv <src> <dst>       move / rename a file or directory\n");
    emit_str("  rm <path>            remove a file or empty directory\n");
    emit_str("  find <name>          search the whole filesystem\n");
    emit_str("  grep <pat> [file]    search a pattern in a file or pipe\n");
    emit_str("  exec <file>          run an external program (hello, clock)\n");
    emit_str("  ps                   list running processes\n");
    emit_str("  kill <pid>           terminate a process\n");
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) emit(" ", 1);
        emit_str(argv[i]);
    }
    emit("\n", 1);
}

static void cmd_ls(int argc, char **argv) {
    uint32_t inode;
    uint8_t  type;
    const char *target = (argc >= 2) ? argv[1] : ".";

    if (resolve(target, &inode, &type, false) != 0) {
        emit_str("ls: no such file or directory\n");
        return;
    }

    if (type != EXT2_FT_DIR) {
        /* List a single file by name */
        char dir[256], base[256];
        split_path(target, dir, base);
        emit_str(base);
        emit("\n", 1);
        return;
    }

    if (list_into(inode, dir_buf, DIR_BUF_SIZE) != 0) {
        emit_str("ls: cannot read directory\n");
        return;
    }

    uint32_t offset = 0;
    while (offset < DIR_BUF_SIZE) {
        struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *) (dir_buf + offset);
        if (e->rec_len == 0) break;
        if (e->inode != 0) {
            uint32_t nl = e->name_len;
            char *nm = entry_name(e);
            emit(nm, nl);
            if (e->file_type == EXT2_FT_DIR) emit("/", 1);
            emit("\n", 1);
        }
        offset += e->rec_len;
    }
}

static void cmd_cd(int argc, char **argv) {
    const char *target = (argc >= 2) ? argv[1] : "/";
    uint8_t type = 0;
    if (resolve(target, NULL, &type, false) != 0 || type != EXT2_FT_DIR ||
        resolve(target, NULL, NULL, true) != 0)
        emit_str("cd: no such directory\n");
}

static void cmd_mkdir(int argc, char **argv) {
    if (argc < 2) { emit_str("mkdir: missing operand\n"); return; }
    uint32_t parent;
    char base[256];
    if (resolve_parent(argv[1], &parent, base) != 0) {
        emit_str("mkdir: invalid path\n");
        return;
    }
    struct EXT2DriverRequest req;
    req.buf          = NULL;
    req.name         = base;
    req.name_len     = (uint8_t) ustrlen(base);
    req.parent_inode = parent;
    req.buffer_size  = 0;
    req.is_directory = true;

    int8_t ret = fs_write(&req);
    if (ret != 0) emit_str("mkdir: failed (exists or invalid parent)\n");
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) { emit_str("cat: missing operand\n"); return; }
    uint32_t parent;
    char base[256];
    if (resolve_parent(argv[1], &parent, base) != 0) { emit_str("cat: invalid path\n"); return; }

    struct EXT2DriverRequest req;
    req.buf          = file_buf;
    req.name         = base;
    req.name_len     = (uint8_t) ustrlen(base);
    req.parent_inode = parent;
    req.buffer_size  = FILE_BUF_SIZE;
    req.is_directory = false;

    uint32_t size = 0;
    if (fs_size(&req, &size) != 0 || size > FILE_BUF_SIZE) {
        emit_str("cat: cannot stat file\n");
        return;
    }
    if (fs_read(&req) != 0) { emit_str("cat: cannot read file\n"); return; }

    emit((char *) file_buf, size);
    if (size == 0 || file_buf[size - 1] != '\n') emit("\n", 1);
}

static void do_copy(const char *src, const char *dst) {
    uint32_t src_parent;
    char     src_base[256];
    if (resolve_parent(src, &src_parent, src_base) != 0) { emit_str("cp: invalid source\n"); return; }

    struct EXT2DriverRequest sreq;
    sreq.buf          = file_buf;
    sreq.name         = src_base;
    sreq.name_len     = (uint8_t) ustrlen(src_base);
    sreq.parent_inode = src_parent;
    sreq.buffer_size  = FILE_BUF_SIZE;
    sreq.is_directory = false;

    uint32_t size = 0;
    if (fs_size(&sreq, &size) != 0 || size > FILE_BUF_SIZE) { emit_str("cp: cannot stat source\n"); return; }
    if (fs_read(&sreq) != 0) { emit_str("cp: cannot read source\n"); return; }

    /* Determine destination parent + name (if dst is a directory, copy into it) */
    uint32_t dst_parent;
    char     dst_base[256];
    uint32_t dst_inode;
    uint8_t  dst_type = 0;
    if (resolve(dst, &dst_inode, &dst_type, false) == 0 && dst_type == EXT2_FT_DIR) {
        dst_parent = dst_inode;
        ustrcpy(dst_base, src_base);
    } else {
        if (resolve_parent(dst, &dst_parent, dst_base) != 0) { emit_str("cp: invalid destination\n"); return; }
    }

    struct EXT2DriverRequest dreq;
    dreq.buf          = file_buf;
    dreq.name         = dst_base;
    dreq.name_len     = (uint8_t) ustrlen(dst_base);
    dreq.parent_inode = dst_parent;
    dreq.buffer_size  = size;
    dreq.is_directory = false;

    if (fs_write(&dreq) != 0) { emit_str("cp: write failed (exists?)\n"); return; }

}

static void cmd_move(int argc, char **argv) {
    if (argc < 3) { emit_str("mv: missing operand\n"); return; }

    uint32_t source_inode, source_parent;
    uint8_t source_type;
    char source_base[256];
    if (resolve(argv[1], &source_inode, &source_type, false) != 0 ||
        resolve_parent(argv[1], &source_parent, source_base) != 0) {
        emit_str("mv: source not found or invalid\n");
        return;
    }
    if (source_type != EXT2_FT_REG_FILE && source_type != EXT2_FT_DIR) {
        emit_str("mv: unsupported source type\n");
        return;
    }

    uint32_t destination_parent, destination_inode;
    uint8_t destination_type;
    char destination_base[256];
    if (resolve(argv[2], &destination_inode, &destination_type, false) == 0 &&
        destination_type == EXT2_FT_DIR) {
        if (destination_inode == source_inode) return;
        destination_parent = destination_inode;
        ustrcpy(destination_base, source_base);
    } else if (resolve_parent(argv[2], &destination_parent, destination_base) != 0) {
        emit_str("mv: invalid destination\n");
        return;
    }

    struct EXT2MoveRequest request = {
        .source_parent_inode = source_parent,
        .source_name = source_base,
        .source_name_len = (uint8_t) ustrlen(source_base),
        .destination_parent_inode = destination_parent,
        .destination_name = destination_base,
        .destination_name_len = (uint8_t) ustrlen(destination_base),
    };
    int8_t ret = fs_move(&request);
    if (ret == 1) emit_str("mv: source not found\n");
    else if (ret == 2) emit_str("mv: destination already exists\n");
    else if (ret == 3) emit_str("mv: invalid parent directory\n");
    else if (ret == 4) emit_str("mv: cannot move root or create a directory cycle\n");
    else if (ret != 0) emit_str("mv: operation failed\n");
}

static void cmd_rm(int argc, char **argv) {
    if (argc < 2) { emit_str("rm: missing operand\n"); return; }
    uint32_t parent;
    char base[256];
    if (resolve_parent(argv[1], &parent, base) != 0) { emit_str("rm: invalid path\n"); return; }

    uint8_t  type = 0;
    uint32_t inode = 0;
    if (resolve(argv[1], &inode, &type, false) != 0) { emit_str("rm: not found\n"); return; }

    struct EXT2DriverRequest req;
    req.buf          = NULL;
    req.name         = base;
    req.name_len     = (uint8_t) ustrlen(base);
    req.parent_inode = parent;
    req.buffer_size  = 0;
    req.is_directory = (type == EXT2_FT_DIR);

    int8_t ret = fs_delete(&req);
    if (ret == 2) emit_str("rm: directory not empty\n");
    else if (ret != 0) emit_str("rm: failed\n");
}

struct FindPendingDirectory {
    uint32_t inode;
    char path[256];
};

static struct FindPendingDirectory find_pending[GROUPS_COUNT * INODES_PER_GROUP];

static void cmd_find(int argc, char **argv) {
    if (argc < 2) { emit_str("find: missing operand\n"); return; }

    uint32_t pending_count = 1;
    find_pending[0].inode = 1;
    find_pending[0].path[0] = '/';
    find_pending[0].path[1] = 0;

    while (pending_count > 0) {
        struct FindPendingDirectory current = find_pending[--pending_count];
        if (list_into(current.inode, dir_buf, DIR_BUF_SIZE) != 0) continue;

        uint32_t offset = 0;
        while (offset < DIR_BUF_SIZE) {
            struct EXT2DirectoryEntry *entry =
                (struct EXT2DirectoryEntry *) (dir_buf + offset);
            if (entry->rec_len < sizeof(*entry) ||
                offset + entry->rec_len > DIR_BUF_SIZE ||
                entry->name_len > entry->rec_len - sizeof(*entry))
                break;
            if (entry->inode != 0) {
                uint32_t name_len = entry->name_len;
                char *name = entry_name(entry);
                bool dot = name_len == 1 && name[0] == '.';
                bool dotdot = name_len == 2 && name[0] == '.' && name[1] == '.';
                if (!dot && !dotdot) {
                    char child[256];
                    build_child(current.path, name, name_len, child);
                    if (name_eq(name, name_len, argv[1])) {
                        emit_str(child);
                        emit("\n", 1);
                    }
                    if (entry->file_type == EXT2_FT_DIR) {
                        if (pending_count < GROUPS_COUNT * INODES_PER_GROUP) {
                            find_pending[pending_count].inode = entry->inode;
                            ustrcpy(find_pending[pending_count].path, child);
                            pending_count++;
                        } else {
                            emit_str("find: directory limit reached\n");
                            return;
                        }
                    }
                }
            }
            offset += entry->rec_len;
        }
    }
}

static void grep_buffer(const char *data, uint32_t len, const char *pattern) {
    uint32_t start = 0;
    for (uint32_t i = 0; i <= len; i++) {
        if (i == len || data[i] == '\n') {
            uint32_t llen = i - start;
            if (contains(data + start, llen, pattern)) {
                emit(data + start, llen);
                emit("\n", 1);
            }
            start = i + 1;
        }
    }
}

static void cmd_grep(int argc, char **argv) {
    if (argc < 2) { emit_str("grep: missing pattern\n"); return; }

    if (argc >= 3) {
        uint32_t parent;
        char base[256];
        if (resolve_parent(argv[2], &parent, base) != 0) { emit_str("grep: invalid path\n"); return; }

        struct EXT2DriverRequest req;
        req.buf          = file_buf;
        req.name         = base;
        req.name_len     = (uint8_t) ustrlen(base);
        req.parent_inode = parent;
        req.buffer_size  = FILE_BUF_SIZE;
        req.is_directory = false;

        uint32_t size = 0;
        if (fs_size(&req, &size) != 0 || size > FILE_BUF_SIZE) { emit_str("grep: cannot stat file\n"); return; }
        if (fs_read(&req) != 0) { emit_str("grep: cannot read file\n"); return; }
        grep_buffer((char *) file_buf, size, argv[1]);
    } else {
        if (pipe_input == NULL) {
            emit_str("grep: missing input file or pipeline\n");
            return;
        }
        grep_buffer(pipe_input, pipe_input_len, argv[1]);
    }
}

/* ============================ Execution ============================ */

/* -- Chapter 4: process management commands -- */

static void cmd_exec(int argc, char **argv) {
    if (argc < 2) { emit_str("exec: missing operand\n"); return; }

    uint32_t parent;
    char base[256];
    if (resolve_parent(argv[1], &parent, base) != 0) { emit_str("exec: invalid path\n"); return; }

    struct EXT2DriverRequest req;
    req.buf          = (void *) 0;
    req.name         = base;
    req.name_len     = (uint8_t) ustrlen(base);
    req.parent_inode = parent;
    req.buffer_size  = 0x100000;
    req.is_directory = false;

    if (proc_exec(&req) != 0)
        emit_str("exec: failed to start program\n");
}

static void cmd_ps(void) {
    emit_str("PID  STATE    NAME\n");
    for (uint32_t i = 0; i < 16; i++) {
        struct ProcessInfo info;
        proc_info(i, &info);
        if (info.state == 0) continue;

        emit_uint(info.pid);
        emit_str("    ");
        const char *state = "?";
        if (info.state == 1) state = "READY";
        else if (info.state == 2) state = "RUNNING";
        else if (info.state == 3) state = "BLOCKED";
        emit_str(state);
        emit_str("  ");

        uint32_t nl = 0;
        while (nl < 32 && info.name[nl]) nl++;
        emit(info.name, nl);
        emit("\n", 1);
    }
}

static void cmd_kill(int argc, char **argv) {
    if (argc < 2) { emit_str("kill: missing operand\n"); return; }

    uint32_t pid = 0;
    for (const char *p = argv[1]; *p; p++) {
        if (*p < '0' || *p > '9') { emit_str("kill: invalid pid\n"); return; }
        uint32_t digit = (uint32_t) (*p - '0');
        if (pid > (0xFFFFFFFFu - digit) / 10u) {
            emit_str("kill: pid out of range\n");
            return;
        }
        pid = pid * 10u + digit;
    }

    if (proc_kill(pid) != 0) emit_str("kill: no such process\n");
}

#define MAX_ARGS 32

static int tokenize(char *line, char **argv) {
    int argc = 0;
    char *p = line;
    while (*p && argc < MAX_ARGS) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    return argc;
}

static void run_command(int argc, char **argv) {
    if (argc == 0) return;
    const char *cmd = argv[0];

    if (ustrcmp(cmd, "help") == 0)        cmd_help();
    else if (ustrcmp(cmd, "clear") == 0)  tty_clear();
    else if (ustrcmp(cmd, "echo") == 0)   cmd_echo(argc, argv);
    else if (ustrcmp(cmd, "cd") == 0)     cmd_cd(argc, argv);
    else if (ustrcmp(cmd, "ls") == 0)     cmd_ls(argc, argv);
    else if (ustrcmp(cmd, "mkdir") == 0)  cmd_mkdir(argc, argv);
    else if (ustrcmp(cmd, "cat") == 0)    cmd_cat(argc, argv);
    else if (ustrcmp(cmd, "cp") == 0) {
        if (argc < 3) emit_str("cp: missing operand\n");
        else do_copy(argv[1], argv[2]);
    }
    else if (ustrcmp(cmd, "mv") == 0) {
        cmd_move(argc, argv);
    }
    else if (ustrcmp(cmd, "rm") == 0)     cmd_rm(argc, argv);
    else if (ustrcmp(cmd, "find") == 0)   cmd_find(argc, argv);
    else if (ustrcmp(cmd, "grep") == 0)   cmd_grep(argc, argv);
    else if (ustrcmp(cmd, "exec") == 0)   cmd_exec(argc, argv);
    else if (ustrcmp(cmd, "ps") == 0)     cmd_ps();
    else if (ustrcmp(cmd, "kill") == 0)   cmd_kill(argc, argv);
    else {
        emit_str(cmd);
        emit_str(": command not found\n");
    }
}

/* Run a full pipeline. Each intermediate stage writes into the alternate buffer. */
static void run_line(char *line) {
    char *stage = line;
    char *input = NULL;
    uint32_t input_len = 0;
    char *output = pipe_a;

    for (;;) {
        char *next = stage;
        while (*next && *next != '|') next++;
        bool has_next = *next == '|';
        if (has_next) *next++ = 0;

        char *argv[MAX_ARGS];
        int argc = tokenize(stage, argv);
        if (argc == 0) {
            if (input != NULL || has_next) {
                pipe_mode = false;
                pipe_input = NULL;
                pipe_output = NULL;
                emit_str("syntax error: empty pipeline stage\n");
            }
            return;
        }

        pipe_input = input;
        pipe_input_len = input_len;
        pipe_mode = has_next;
        pipe_output = output;
        pipe_output_len = 0;
        if (pipe_mode) pipe_output[0] = 0;
        run_command(argc, argv);

        if (!has_next) break;
        input = output;
        input_len = pipe_output_len;
        output = output == pipe_a ? pipe_b : pipe_a;
        stage = next;
    }
    pipe_input = NULL;
    pipe_input_len = 0;
    pipe_output = NULL;
    pipe_output_len = 0;
    pipe_mode = false;
}

/* ============================ Entry point ============================ */

int main(void) {
    kb_activate();
    tty_clear();

    emit_str("  _  __                  ____  _____\n");
    emit_str(" | |/ /_ __ ___  _ __   / __ \\/ ___/\n");
    emit_str(" | ' /| '__/ _ \\| '_ \\ | |  | \\__ \\\n");
    emit_str(" | . \\| | | (_) | | | || |__| |___) |\n");
    emit_str(" |_|\\_\\_|  \\___/|_| |_| \\____/|____/\n");
    emit_str("KronOS Shell - type 'help' for available commands\n\n");

    char   line[256];
    char   prompt[300];

    for (;;) {
        char cwd[256];
        cwd_path(cwd);

        uint32_t k = 0;
        const char *pre = "user@KronOS:";
        for (uint32_t i = 0; pre[i]; i++) prompt[k++] = pre[i];
        for (uint32_t i = 0; cwd[i]; i++) prompt[k++] = cwd[i];
        prompt[k++] = '$';
        prompt[k++] = ' ';
        prompt[k] = 0;

        emit_str(prompt);
        read_line(line, sizeof(line));
        run_line(line);
    }
}
