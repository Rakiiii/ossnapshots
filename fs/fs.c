#include <inc/string.h>
#include <inc/partition.h>
#include <inc/time.h>

#include "fs.h"

/* Superblock */
struct Super *super;
/* Bitmap blocks mapped in memory */
uint32_t *bitmap;

/********************************************************** snapshot region *****************/

uint64_t *root_snapshot_file = 0;
uint64_t *snapshot_file_dir = 0;
uint64_t *snapshot_config_file = 0;
uint64_t *current_snapshot_file = 0;

bool SNAPSHOT_DEBUG = true;

#define printf_debug(M, ...) if (SNAPSHOT_DEBUG) cprintf("[%s:%d] Note: " M "\n",__FILE__, __LINE__,##__VA_ARGS__);

#define snapshot_name(variable,name) \
    char variable[MAXNAMELEN];       \
    variable[0] = '\0';              \
    strcat(variable, SNAPDIR);       \
    strcat(variable, name);          \

#define snapshoted_file_name(variable,file_name,snapshot_name)  \
    char variable[MAXNAMELEN];                                  \
    variable[0] = '\0';                                         \
    strcat(variable, SNAPDIR);                                  \
    strcat(variable, file_name);                                 \
    strcat(variable, SNAPFILESEP);                               \
    strcat(variable, snapshot_name);                             \

#define to_file(ptr) ((struct File *)(*ptr))

#define PTROFFSET 1

//Функция, выводящая актуальное состояние bitmap
// 0 если занят
int 
debug_print_bitmap() {
    char res;
	
    for (blockno_t blockno = 0; blockno < super->s_nblocks; ++blockno) {

        if (blockno % 32 == 0) {
            printf_debug("\n");
        }

        res = (bitmap[blockno / 32] & (1 << (blockno % 32))) >> (blockno % 32);
        
        printf_debug("%d",res);
  }

  printf_debug("\n");

  return 0;
}


static bool
internal_print_snapshot_list(struct File *snap, struct Snapshot_header header);

static int
strcmp_snapshoted(char *snapshoted_file_name, char *file_name);
/********************************************************** snapshot endregion *************/

/****************************************************************
 *                         Super block
 ****************************************************************/

/* Validate the file system super-block. */
void
check_super(void) {
    if (super->s_magic != FS_MAGIC)
        panic("bad file system magic number %08x", super->s_magic);

    if (super->s_nblocks > DISKSIZE / BLKSIZE)
        panic("file system is too large");

    cprintf("superblock is good\n");
}

/****************************************************************
 *                         Free block bitmap
 ****************************************************************/

/* Check to see if the block bitmap indicates that block 'blockno' is free.
 * Return 1 if the block is free, 0 if not. */
bool
block_is_free(uint32_t blockno) {
    if (super == 0 || blockno >= super->s_nblocks) return 0;
    if (TSTBIT(bitmap, blockno)) return 1;
    return 0;
}

/* Mark a block free in the bitmap */
void
free_block(uint32_t blockno) {
    /* Blockno zero is the null pointer of block numbers. */
    if (blockno == 0) panic("attempt to free zero block");
    SETBIT(bitmap, blockno);
}

/* Search the bitmap for a free block and allocate it.  When you
 * allocate a block, immediately flush the changed bitmap block
 * to disk.
 *
 * Return block number allocated on success,
 * 0 if we are out of blocks.
 *
 * Hint: use free_block as an example for manipulating the bitmap. */
blockno_t
alloc_block(void) {
    /* The bitmap consists of one or more blocks.  A single bitmap block
     * contains the in-use bits for BLKBITSIZE blocks.  There are
     * super->s_nblocks blocks in the disk altogether. */

    // LAB 10: Your code here
    for (blockno_t blockno = 1; blockno < super->s_nblocks; blockno++) {
        if (block_is_free(blockno)) {
            CLRBIT(bitmap, blockno);
            flush_block(&bitmap[blockno / 32]);
            return blockno;
        }
    }

    return -E_NO_DISK;
}

/* Validate the file system bitmap.
 *
 * Check that all reserved blocks -- 0, 1, and the bitmap blocks themselves --
 * are all marked as in-use. */
void
check_bitmap(void) {

    /* Make sure all bitmap blocks are marked in-use */
    for (uint32_t i = 0; i * BLKBITSIZE < super->s_nblocks; i++)
        assert(!block_is_free(2 + i));

    /* Make sure the reserved and root blocks are marked in-use. */

    assert(!block_is_free(1));
    assert(!block_is_free(0));

    cprintf("bitmap is good\n");
}

/****************************************************************
 *                    File system structures
 ****************************************************************/

/* Initialize the file system */
void
sh_cfg_init(void) {
    printf_debug("Start snapshot cfg init\n");

    struct Snapshot_config cfg = {
        .root_snapshot_name = ROOTSNAP,
        .current_snapshot_name = ROOTSNAP};

    if(pure_file_write((struct File *) *snapshot_config_file, &cfg, sizeof(struct Snapshot_config), HEADERPOS) != sizeof(struct Snapshot_config)) {
        panic("Snapshot config cannot be inited\n");
    } else {
        printf_debug("Snapshot config inited\n");
    }
}

void 
sh_root_init(void) {
    printf_debug("Start root cfg init\n");

    struct Snapshot_header root_header = {
        .comment = "Its root comment",
        .date = sys_gettime(),
        .is_deleted = false,
        .prev_snapshot = 0};

    printf_debug("Root header inited\n");

    int alloc_block_result;
    char * addr;

    if ((alloc_block_result = alloc_block()) == 0) {
        panic("Out of memory! Root snapshot cannot be created \n");
    }

    printf_debug("Old bitmap block for root snapshot is alloced\n");

    root_header.old_bitmap = alloc_block_result;
    addr = diskaddr(alloc_block_result);
    memcpy(addr, diskaddr(1), BLKSIZE);
    flush_block(addr);

    printf_debug("Root snapshot old bitmap block flushed\n");
    
    if (pure_file_write((struct File *) *root_snapshot_file, &root_header, sizeof(struct Snapshot_header), HEADERPOS) != sizeof(struct Snapshot_header)) {
        panic("Root snapshot cannot be inited\n");
    } else {
        printf_debug("Root snapshot inited\n");
    }
}

void
sh_curr_snap_init(void) {
    printf_debug("Start current snapshot init\n");

    struct Snapshot_config cfg;

    if (pure_file_read((struct File *) *snapshot_config_file, &cfg, sizeof(struct Snapshot_config), HEADERPOS) != sizeof(struct Snapshot_config)) {
        panic("Cannot read cfg for snapshot to init current snapshot\n");
    }
    
    snapshot_name(snapshot_name, cfg.current_snapshot_name);

    if(file_open(snapshot_name, (struct File **) current_snapshot_file) != 0) {
        panic("Cannot open current snapshot with path %s, cfg value is %s\n", snapshot_name, cfg.current_snapshot_name);
    } else {
        printf_debug("Current snapshot inited\n");
    }
}

void 
sh_init(void) {
    printf_debug("Start sh init\n");

    int res;

    root_snapshot_file = (uint64_t *) (super + PTROFFSET);
    
    snapshot_file_dir = (uint64_t *) (root_snapshot_file + PTROFFSET);

    snapshot_config_file =  (uint64_t *) (snapshot_file_dir + PTROFFSET);

    current_snapshot_file = (uint64_t *) (snapshot_config_file + PTROFFSET);

    printf_debug("Pointers inited\n");

    if((res = pure_file_create(SNAPDIR, (struct File **)snapshot_file_dir)) == 0) {
        printf_debug("Snapshot dir created\n");
        ((struct File *)(*snapshot_file_dir))->f_type = FTYPE_DIR;
        printf_debug("Snapshot file type dir inited\n");
    } else if (res == -E_FILE_EXISTS){
        printf_debug("Snapshot dir ptr inited");
    } else {
        panic("Snapshot dir cannot be created");
    }

    if ((res = pure_file_create(SNAPCFG, (struct File **)snapshot_config_file)) == 0)  {
        printf_debug("Snapshot config created\n");
        sh_cfg_init();
    } else if (res == -E_FILE_EXISTS){
        printf_debug("Snapshot config ptr inited\n");
    } else {
        panic("Snapshot config cannot be created\n");
    }

    snapshot_name(root_snap_name, ROOTSNAP);

    if ((res = pure_file_create(root_snap_name, (struct File **)root_snapshot_file)) == 0) {
        printf_debug("Root snapshot created\n");
        sh_root_init();
    } else if (res == -E_FILE_EXISTS){
        printf_debug("Root snapshot ptr inited\n");
    } else {
        panic("Snapshot config cannot be created\n");
    }

    sh_curr_snap_init();
}

void
fs_init(void) {
    static_assert(sizeof(struct File) == 256, "Unsupported file size");

    printf_debug("Start fs init\n");

    /* Find a JOS disk.  Use the second IDE disk (number 1) if availabl */
    if (ide_probe_disk1())
        ide_set_disk(1);
    else
        ide_set_disk(0);
    bc_init();

    /* Set "super" to point to the super block. */
    super = diskaddr(1);

    check_super();

    /* Set "bitmap" to the beginning of the first bitmap block. */
    bitmap = diskaddr(2);

    check_bitmap();

    sh_init();

    fs_sync();

    fs_create_tmp_snapshot();
}

/* Find the disk block number slot for the 'filebno'th block in file 'f'.
 * Set '*ppdiskbno' to point to that slot.
 * The slot will be one of the f->f_direct[] entries,
 * or an entry in the indirect block.
 * When 'alloc' is set, this function will allocate an indirect block
 * if necessary.
 *
 * Returns:
 *  0 on success (but note that *ppdiskbno might equal 0).
 *  -E_NOT_FOUND if the function needed to allocate an indirect block, but
 *      alloc was 0.
 *  -E_NO_DISK if there's no space on the disk for an indirect block.
 *  -E_INVAL if filebno is out of range (it's >= NDIRECT + NINDIRECT).
 *
 * Analogy: This is like pgdir_walk for files.
 * Hint: Don't forget to clear any block you allocate. */
int
file_block_walk(struct File *f, blockno_t filebno, blockno_t **ppdiskbno, bool alloc) {
    // LAB 10: Your code here
    if (filebno >= NDIRECT + NINDIRECT) {
        return -E_INVAL;
    }
    if (filebno < NDIRECT) {
        *ppdiskbno = (uint32_t*)f->f_direct + filebno;
    } else {
        if (!f->f_indirect) {
            if (!alloc) {
                return -E_NOT_FOUND;
            }
            blockno_t block = alloc_block();
            if (!block) {
                return -E_NO_DISK;
            }
            f->f_indirect = block;
            memset(diskaddr(f->f_indirect), 0, BLKSIZE);
        }
        *ppdiskbno = (uint32_t *)diskaddr(f->f_indirect) + filebno - NDIRECT;
    }
    return 0;
}

/* Set *blk to the address in memory where the filebno'th
 * block of file 'f' would be mapped.
 *
 * Returns 0 on success, < 0 on error.  Errors are:
 *  -E_NO_DISK if a block needed to be allocated but the disk is full.
 *  -E_INVAL if filebno is out of range.
 *
 * Hint: Use file_block_walk and alloc_block. */
int
file_get_block(struct File *f, uint32_t filebno, char **blk) {
    // LAB 10: Your code here
    uint32_t *pdiskbno;
    file_block_walk(f, filebno, &pdiskbno, 1);
    if (!*pdiskbno) {
        blockno_t block = alloc_block();
        if (!block) {
            return -E_NO_DISK;
        }
        *pdiskbno = block;
    }
    *blk = (char *)diskaddr(*pdiskbno);
    return 0;
}

/* Try to find a file named "name" in dir.  If so, set *file to it.
 *
 * Returns 0 and sets *file on success, < 0 on error.  Errors are:
 *  -E_NOT_FOUND if the file is not found */
static int
dir_lookup(struct File *dir, const char *name, struct File **file) {
    /* Search dir for name.
     * We maintain the invariant that the size of a directory-file
     * is always a multiple of the file system's block size. */
    assert((dir->f_size % BLKSIZE) == 0);
    blockno_t nblock = dir->f_size / BLKSIZE;
    for (blockno_t i = 0; i < nblock; i++) {
        char *blk;
        int res = file_get_block(dir, i, &blk);
        if (res < 0) return res;

        struct File *f = (struct File *)blk;
        for (blockno_t j = 0; j < BLKFILES; j++)
            if (strcmp(f[j].f_name, name) == 0) {
                *file = &f[j];
                return 0;
            }
    }
    return -E_NOT_FOUND;
}

/* Set *file to point at a free File structure in dir.  The caller is
 * responsible for filling in the File fields. */
static int
dir_alloc_file(struct File *dir, struct File **file) {
    char *blk;

    assert((dir->f_size % BLKSIZE) == 0);
    blockno_t nblock = dir->f_size / BLKSIZE;
    for (blockno_t i = 0; i < nblock; i++) {
        
        int res = file_get_block(dir, i, &blk);
        if (res < 0) return res;

        struct File *f = (struct File *)blk;
        for (blockno_t j = 0; j < BLKFILES; j++) {
            if (f[j].f_name[0] == '\0') {
                *file = &f[j];
                return 0;
            }
        }
    }
    dir->f_size += BLKSIZE;
    int res = file_get_block(dir, nblock, &blk);
    if (res < 0) return res;

    *file = (struct File *)blk;
    return 0;
}

/* Skip over slashes. */
static const char *
skip_slash(const char *p) {
    while (*p == '/')
        p++;
    return p;
}

/* Evaluate a path name, starting at the root.
 * On success, set *pf to the file we found
 * and set *pdir to the directory the file is in.
 * If we cannot find the file but find the directory
 * it should be in, set *pdir and copy the final path
 * element into lastelem. */
static int
walk_path(const char *path, struct File **pdir, struct File **pf, char *lastelem) {
    // printf_debug("Walk_path path: %s\n", path);

    const char *p;
    char name[MAXNAMELEN];
    struct File *dir, *f;
    int r;

    //if (*path != '/')
    //    return -E_BAD_PATH;
    path = skip_slash(path);
    f = &super->s_root;
    dir = 0;
    name[0] = 0;

    if (pdir)
        *pdir = 0;
    *pf = 0;

    while (*path != '\0') {
        dir = f;
        p = path;
        while (*path != '/' && *path != '\0')
            path++;
        if (path - p >= MAXNAMELEN)
            return -E_BAD_PATH;

        memmove(name, p, path - p);
        name[path - p] = '\0';
        path = skip_slash(path);

        if (dir->f_type != FTYPE_DIR)
            return -E_NOT_FOUND;

        if ((r = dir_lookup(dir, name, &f)) < 0) {
            if (r == -E_NOT_FOUND && *path == '\0') {
                if (pdir)
                    *pdir = dir;
                if (lastelem)
                    strcpy(lastelem, name);

                *pf = 0;
            }
            return r;
        }
    }

    if (pdir)
        *pdir = dir;
    *pf = f;
    return 0;
}

/****************************************************************
 *                        File operations
 ****************************************************************/

/* Create "path".  On success set *pf to point at the file and return 0.
 * On error return < 0. */
int
file_create(const char *path, struct File **pf) {

    printf_debug("Start of creating file %s\n", path);

    char name[MAXNAMELEN];
    int res;
    struct File *dir, *filp;

    if ((res = walk_path(path, &dir, &filp, name)) == 0) { 
        printf_debug("Two files with same names cannot be created: %s\n", name);
        return -E_FILE_EXISTS;
    }
    if (res != -E_NOT_FOUND || dir == 0) return res;
    if ((res = dir_alloc_file(dir, &filp)) < 0) return res;

    strcpy(filp->f_name, name);

    *pf = filp;
    pure_file_flush(dir);

    // todo adding created files ptr to special field in snapshot header

    return 0;
}

int
pure_file_create(const char *path, struct File **pf) {
    printf_debug("Start of creating file %s\n", path);

    char name[MAXNAMELEN];
    int res;
    struct File *dir, *filp;

    if ((res = walk_path(path, &dir, &filp, name)) == 0) { 
        printf_debug("Two files with same names cannot be created: %s\n", name);
        return -E_FILE_EXISTS;
    }
    if (res != -E_NOT_FOUND || dir == 0) return res;
    if ((res = dir_alloc_file(dir, &filp)) < 0) return res;

    strcpy(filp->f_name, name);

    *pf = filp;
    pure_file_flush(dir);

    return 0;
}

/* Open "path".  On success set *pf to point at the file and return 0.
 * On error return < 0. */
int
file_open(const char *path, struct File **pf) {
    return walk_path(path, 0, pf, 0);
}

/* Read count bytes from f into buf, starting from seek position
 * offset.  This meant to mimic the standard pread function.
 * Returns the number of bytes read, < 0 on error. */

ssize_t
file_read(struct File *f, void *buf, size_t count, off_t offset) {
    printf_debug("Try to read from file %s...\n", f->f_name);

    struct File *tmp_file;

    struct File *tmp_snapshot_file = to_file(current_snapshot_file);

    tmp_file = f;

    if(resolve_file_for_read(&tmp_file, tmp_snapshot_file) != 0) {
        return -E_INVAL;
    }

    return pure_file_read(tmp_file, buf, count, offset);
}

// ssize_t
// file_read(struct File *f, void *buf, size_t count, off_t offset) {
//     if (*old_current_snapshot_file != 0 && find_in_snapshot_list(f) == false && f->f_type != FTYPE_DIR) {
//         return snapshot_file_read(f, buf, count, offset);
//     } else {
//         return pure_file_read(f, buf, count, offset);
//     }
// }

// Чтение файлов с диска без учета снэпшотов
ssize_t
pure_file_read(struct File *f, void *buf, size_t count, off_t offset) {
    int r, bn;
    off_t pos;
    char *blk;

    if (offset >= f->f_size) {
        return 0;
    }

    count = MIN(count, f->f_size - offset);

    for (pos = offset; pos < offset + count;) {
        if ((r = file_get_block(f, pos / BLKSIZE, &blk)) < 0) {
            return r;
        }
        bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
        memmove(buf, blk + pos % BLKSIZE, bn);
        pos += bn;
        buf += bn;
    }

    return count;
}

// ssize_t
// file_read(struct File *f, void *buf, size_t count, off_t offset) {
//     char *blk;

//     if (offset >= f->f_size)
//         return 0;

//     count = MIN(count, f->f_size - offset);

//     for (off_t pos = offset; pos < offset + count;) {
//         int r = file_get_block(f, pos / BLKSIZE, &blk);
//         if (r < 0) return r;

//         int bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
//         memmove(buf, blk + pos % BLKSIZE, bn);
//         pos += bn;
//         buf += bn;
//     }

//     return count;
// }

/* Write count bytes from buf into f, starting at seek position
 * offset.  This is meant to mimic the standard pwrite function.
 * Extends the file if necessary.
 * Returns the number of bytes written, < 0 on error. */


ssize_t
file_write(struct File *f, const void *buf, size_t count, off_t offset) {
    printf_debug("Try to write to file %s...\n", f->f_name);

    struct File *tmp_file;

    struct File *tmp_snapshot_file = to_file(current_snapshot_file);

    tmp_file = f;

    if(resolve_file_for_write(&tmp_file, tmp_snapshot_file) != 0) {
        return -E_INVAL;
    }

    return pure_file_write(tmp_file, buf, count, offset);
}

// ssize_t
// file_write(struct File *f, const void *buf, size_t count, off_t offset) {

//     printf_debug("writing in file %s...\n",f->f_name);

//     if (*old_current_snapshot_file != 0 && find_in_snapshot_list(f) == false) { 
//         if (find_in_snapshot_list(f) == 0) {
//             return snapshot_file_write(f, buf, count, offset);
//         }
//     }

//     return pure_file_write(f, buf, count, offset);
// }

// Запись в файл без учета снапшотов
ssize_t
pure_file_write(struct File *f, const void *buf, size_t count, off_t offset) {
    printf_debug("Value %s writen to file %s\n",(char *)buf, f->f_name);
    int res;

    /* Extend file if necessary */
    if (offset + count > f->f_size)
        // todo:: Chancge to pure???
        if ((res = pure_file_set_size(f, offset + count)) < 0) return res;

    for (off_t pos = offset; pos < offset + count;) {
        char *blk;
        if ((res = file_get_block(f, pos / BLKSIZE, &blk)) < 0) return res;

        uint32_t bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
        memmove(blk + pos % BLKSIZE, buf, bn);
        pos += bn;
        buf += bn;
    }

    return count;
}

// ssize_t
// file_write(struct File *f, const void *buf, size_t count, off_t offset) {
//     int res;

//     /* Extend file if necessary */
//     if (offset + count > f->f_size)
//         if ((res = file_set_size(f, offset + count)) < 0) return res;

//     for (off_t pos = offset; pos < offset + count;) {
//         char *blk;
//         if ((res = file_get_block(f, pos / BLKSIZE, &blk)) < 0) return res;

//         uint32_t bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
//         memmove(blk + pos % BLKSIZE, buf, bn);
//         pos += bn;
//         buf += bn;
//     }

//     return count;
// }

/* Remove a block from file f.  If it's not there, just silently succeed.
 * Returns 0 on success, < 0 on error. */
static int
file_free_block(struct File *f, uint32_t filebno) {
    blockno_t *ptr;
    int res = file_block_walk(f, filebno, &ptr, 0);
    if (res < 0) return res;

    if (*ptr) {
        free_block(*ptr);
        *ptr = 0;
    }
    return 0;
}

/* Remove any blocks currently used by file 'f',
 * but not necessary for a file of size 'newsize'.
 * For both the old and new sizes, figure out the number of blocks required,
 * and then clear the blocks from new_nblocks to old_nblocks.
 * If the new_nblocks is no more than NDIRECT, and the indirect block has
 * been allocated (f->f_indirect != 0), then free the indirect block too.
 * (Remember to clear the f->f_indirect pointer so you'll know
 * whether it's valid!)
 * Do not change f->f_size. */
static void
file_truncate_blocks(struct File *f, off_t newsize) {
    blockno_t old_nblocks = CEILDIV(f->f_size, BLKSIZE);
    blockno_t new_nblocks = CEILDIV(newsize, BLKSIZE);
    for (blockno_t bno = new_nblocks; bno < old_nblocks; bno++) {
        int res = file_free_block(f, bno);
        if (res < 0) cprintf("warning: file_free_block: %i", res);
    }

    if (new_nblocks <= NDIRECT && f->f_indirect) {
        free_block(f->f_indirect);
        f->f_indirect = 0;
    }
}

/* Set the size of file f, truncating or extending as necessary. */

int
file_set_size(struct File *f, off_t newsize) {
    printf_debug("Try to set new size %d for file %s\n", (int)newsize, f->f_name);

    // find file in snapshot
    // if deleting -> delete real file
    // if not found create copy
    // then modify original and copy

    struct File *tmp_file;

    struct File *tmp_snapshot_file = to_file(current_snapshot_file);

    tmp_file = f;

    if(resolve_file_for_write(&tmp_file, tmp_snapshot_file) != 0) {
        return -E_INVAL;
    }

    // todo remove from test set size
    // if (tmp_file != f) {
    //     f->f_size = newsize;
    // }
    
    return pure_file_set_size(tmp_file, newsize);
}

int
pure_file_set_size(struct File *f, off_t newsize) {
    printf_debug("Setting new size %d old size is %d, for file %s\n",(int)newsize, (int)f->f_size, f->f_name);

    if (f->f_size > newsize)
        file_truncate_blocks(f, newsize);
    f->f_size = newsize;
    flush_block(f);
    return 0;
}

// int
// file_set_size(struct File *f, off_t newsize) {
//     if (f->f_size > newsize)
//         file_truncate_blocks(f, newsize);
//     f->f_size = newsize;
//     flush_block(f);
//     return 0;
// }

/* Flush the contents and metadata of file f out to disk.
 * Loop over all the blocks in file.
 * Translate the file block number into a disk block number
 * and then check whether that disk block is dirty.  If so, write it out. */

void
file_flush(struct File *f) {
    printf_debug("Start flushing file %s\n", f->f_name);

    struct File *tmp_file;

    struct File *tmp_snapshot_file = to_file(current_snapshot_file);

    tmp_file = f;

    if(resolve_file_for_read(&tmp_file, tmp_snapshot_file) != 0) {
        printf_debug("File %s cannot be flushed, because nothing was writen to it\n", f->f_name);
    }

    pure_file_flush(tmp_file);
}

void
pure_file_flush(struct File *f) {
    blockno_t *pdiskbno;

    for (blockno_t i = 0; i < CEILDIV(f->f_size, BLKSIZE); i++) {
        if (file_block_walk(f, i, &pdiskbno, 0) < 0 ||
            pdiskbno == NULL || *pdiskbno == 0)
            continue;
        flush_block(diskaddr(*pdiskbno));
    }
    if (f->f_indirect)
        flush_block(diskaddr(f->f_indirect));
    flush_block(f);
}

/* Sync the entire file system.  A big hammer. */
void
fs_sync(void) {
    for (int i = 1; i < super->s_nblocks; i++) {
        flush_block(diskaddr(i));
    }
}



/********************** snapshot start ***************************************************************/

/********************** utils start ******************************************************************/

// выбирает правильный файл для чтения снапшотированных данных
int
resolve_file_for_read(struct File **pfile, struct File *snapshot_file) {
    printf_debug("Resolving real file for read for %s with current snapshot %s\n", (*pfile)->f_name, snapshot_file->f_name);

    struct File *file, *tmp_file;
    struct Snapshot_header snapshot_header;
    int counter;

    file = *pfile;

    if(pure_file_read(snapshot_file, &snapshot_header, HEADERSIZE, HEADERPOS) != HEADERSIZE) {
        printf_debug("Header for snapshot %s cannot be readed\n", snapshot_file->f_name);
        return -E_INVAL;
    }

    counter = 0;
    tmp_file = (struct File *) snapshot_header.modified_files[counter];

    while(tmp_file != 0) {
        printf_debug("Checking file %s from snapshot %s\n", tmp_file->f_name, snapshot_file->f_name);

        if (strcmp_snapshoted(tmp_file->f_name, file->f_name) == 0) {
            printf_debug("File for read %s founded in snapshot %s and have name %s\n", file->f_name, snapshot_file->f_name, tmp_file->f_name);

            *pfile = tmp_file;
            
            return 0;
        }

        ++counter;
        tmp_file = (struct File *) snapshot_header.modified_files[counter];
    }

    if (snapshot_header.prev_snapshot != 0) {
        return resolve_file_for_read(pfile, (struct File *)snapshot_header.prev_snapshot);
    } else {
        printf_debug("File %s for read not founded in current snapshot\n", file->f_name);
        return 0;
    }
}

int
resolve_file_for_write(struct File **pfile, struct File *snapshot_file) {
    printf_debug("Resolving real file for wtite for %s with current snapshot %s\n", (*pfile)->f_name, snapshot_file->f_name);

    struct File *file, *tmp_file;
    struct Snapshot_header snapshot_header;
    int counter, res;

    file = *pfile;

    if(pure_file_read(snapshot_file, &snapshot_header, HEADERSIZE, HEADERPOS) != HEADERSIZE) {
        printf_debug("Cannot read header for snapshot %s to resolve file %s for write\n", snapshot_file->f_name, (*pfile)->f_name);
        return -E_INVAL;
    }

    counter = 0;
    tmp_file = (struct File *) snapshot_header.modified_files[counter];

    while(tmp_file != 0) {
        printf_debug("Checking file %s from snapshot %s\n", tmp_file->f_name, snapshot_file->f_name);

        if (strcmp_snapshoted(tmp_file->f_name, file->f_name) == 0) {
            printf_debug("File for write %s founded in snapshot %s and have name %s\n", file->f_name, snapshot_file->f_name, tmp_file->f_name);

            *pfile = tmp_file;
            
            return 0;
        }

        ++counter;
        tmp_file = (struct File *) snapshot_header.modified_files[counter];
    }
    
    printf_debug("File %s not founded in current snapshot, it must be created\n", file->f_name);

    if((res = resolve_file_for_read(pfile, snapshot_file)) < 0) {
        printf_debug("File for write %s cannot be resolved because file for read %s cannot be found in %s\n",file->f_name, file->f_name, snapshot_file->f_name);
        return res;
    }

    struct File *pfile_for_write;

    if((res = create_and_copy(&pfile_for_write, pfile)) != (*pfile)->f_size) {
        printf_debug("File %s for write cannot be CAC: error code %d\n", file->f_name, res);
        return res;
    }

    *pfile = pfile_for_write;

    snapshot_header.modified_files[counter] = (uint64_t) pfile_for_write;

    if(pure_file_write(snapshot_file, &snapshot_header, HEADERSIZE, HEADERPOS) != HEADERSIZE) {
        printf_debug("Cannot write header for snapshot %s after resolving file %s for write\n", snapshot_file->f_name, file->f_name);
        return -E_INVAL;
    }

    pure_file_flush(snapshot_file);

    return 0;
}

static int
strcmp_snapshoted(char *sfile_name, char *file_name) {
    char *pseparator = strfind(sfile_name + 1, SNAPFILESEP[0]);

    int separator_position = pseparator - sfile_name;

    return strncmp(sfile_name, file_name, separator_position);
}



int 
create_and_copy(struct File **dstfile, struct File **srcfile) {

    printf_debug("Start CAC for file %s\n", (*srcfile)->f_name);

    off_t offset = 0;
    off_t cac_range = (*srcfile)->f_size; 

    uint32_t bn;

    off_t position_in_file;

    int file_create_result, src_get_block_result, dst_get_block_result, file_set_size_result;

    char *src_file_block, *dst_file_block;

    char *current_snapshot_name = to_file(current_snapshot_file)->f_name;

    snapshoted_file_name(snapshoted_file_name, (*srcfile)->f_name, current_snapshot_name);
    
    file_create_result = pure_file_create(snapshoted_file_name, dstfile);

    if (file_create_result == -E_FILE_EXISTS) {
        printf_debug("File for snapshoted file %s already exist\n", snapshoted_file_name);
        return file_create_result;
    } else if(file_create_result == -E_NOT_FOUND) {
        printf_debug("Directory for snapshoted file %s not found\n", snapshoted_file_name);
        return file_create_result;
    } else if(file_create_result < 0) {
        printf_debug("File for snapshoted file %s cannot be created: error code %d\n", snapshoted_file_name, file_create_result);
        return file_create_result;
    }

    if ((*srcfile)->f_size > (*dstfile)->f_size) {
        if ((file_set_size_result = pure_file_set_size(*dstfile, (*srcfile)->f_size)) < 0) {
            printf_debug("Cannot set new size for file %s in CAC from file %s\n", (*dstfile)->f_name, (*srcfile)->f_name);
            return file_set_size_result;
        }
    }

    for (position_in_file = offset; position_in_file < cac_range;) {

        // Читаем нужный блок для копированния
        if ((src_get_block_result = file_get_block(*srcfile, position_in_file / BLKSIZE, &src_file_block)) < 0) {
            printf_debug("Cannot get block for file %s at pos %d with block number %lld\n", (*srcfile)->f_name, position_in_file, position_in_file / BLKSIZE);
            return src_get_block_result;
        }

        // Читаем новый блок для записи
        if ((dst_get_block_result = file_get_block(*dstfile, position_in_file / BLKSIZE, &dst_file_block)) < 0) {
            printf_debug("Cannot get block for file %s at pos %d with block number %lld\n", (*dstfile)->f_name, position_in_file, position_in_file / BLKSIZE);
            return src_get_block_result;
        }

        bn = MIN(BLKSIZE - position_in_file % BLKSIZE, (*srcfile)->f_size - position_in_file);

        memmove(dst_file_block + position_in_file % BLKSIZE , src_file_block + position_in_file % BLKSIZE, bn);
        position_in_file += bn;
    }

    printf_debug("CAC for file %s ends, file with name %s created and filed\n", (*srcfile)->f_name, (*dstfile)->f_name);

    return cac_range - offset;
}

/********************** utils end   ******************************************************************/

/********************** logic start ******************************************************************/



/********************** logic end   ******************************************************************/

/************************ functions start ************************************************************/
int fs_create_tmp_snapshot() {
    printf_debug("Start creatin temporary snapshot file\n");

    struct File *tmp_snapshot_file, *actual_snapshot_file; //*buffer_snapshot_file;

    struct Snapshot_header actual_snapshot_header;// buffer_snapshot_header;

    int file_create_result, header_write_result, header_read_result, alloc_block_result, cfg_read_result, cfg_write_result;

    char * addr;

    snapshot_name(tmp_snap_name, TMPSNAP);

    file_create_result = pure_file_create(tmp_snap_name, &tmp_snapshot_file);

    if (file_create_result == -E_FILE_EXISTS) {
        printf_debug("File for temporary snapshot already exist\n");
        return file_create_result;
    } else if(file_create_result == -E_NOT_FOUND) {
        printf_debug("Directory for file for snapshot not found\n");
        return file_create_result;
    } else if(file_create_result < 0) {
        printf_debug("Temporary file for snapshot cannot be created: error code %d\n", file_create_result);
        return file_create_result;
    }

    printf_debug("Temporary snapshot file created\n");

    actual_snapshot_file = to_file(current_snapshot_file);

    if ((header_read_result = pure_file_read(actual_snapshot_file, &actual_snapshot_header, HEADERSIZE, HEADERPOS)) != HEADERSIZE) {
        printf_debug("Current snapshot header cannot be readed: error code %d\n", header_read_result);
        return header_read_result;
    } else {
        printf_debug("Current snapshot header readed\n");
    }

    struct Snapshot_header tmp_snapshot_header = {
        .comment = "",
        .date = 0,
        .is_deleted = false,
        .prev_snapshot = (uint64_t)actual_snapshot_file};

    // А надо ли это делать???
    int counter = 0;
    while(actual_snapshot_header.next_snapshot[counter] != 0) {

        // buffer_snapshot_file = (struct File *) actual_snapshot_header.next_snapshot[counter];

        // tmp_snapshot_header.next_snapshot[counter] = actual_snapshot_header.next_snapshot[counter];

        // if ((header_read_result = pure_file_read(buffer_snapshot_file, &buffer_snapshot_header, HEADERSIZE, HEADERPOS)) != HEADERSIZE) {
        //     printf_debug("Next snapshot header cannot be readed: error code %d\n", header_read_result);
        //     return header_read_result;
        // } else {
        //     printf_debug("Next snapshot header readed\n");
        // }        

        // buffer_snapshot_header.prev_snapshot = (uint64_t) tmp_snapshot_file;

        // if ((header_write_result = pure_file_write(tmp_snapshot_file, &tmp_snapshot_header, HEADERSIZE, HEADERPOS)) != HEADERSIZE) {
        //     printf_debug("Next snapshot header cannot be updated\n");
        //     return header_write_result;
        // } else {
        //     printf_debug("Next snapshot header updated\n");
        // }

        // pure_file_flush(buffer_snapshot_file);

        ++counter;
    }

    actual_snapshot_header.next_snapshot[counter] = (uint64_t) tmp_snapshot_file;

    if ((header_write_result = pure_file_write(actual_snapshot_file, &actual_snapshot_header, HEADERSIZE, HEADERPOS)) != HEADERSIZE) {
        printf_debug("Current snapshot header cannot be updated\n");
        return header_write_result;
    } else {
        printf_debug("Current snapshot header updated\n");
    }

    pure_file_flush(actual_snapshot_file);

    if ((alloc_block_result = alloc_block()) == 0) {
        panic("Out of memory! Temporary snapshot cannot be created \n");
    }

    printf_debug("Old bitmap block for temporary snapshot is alloced\n");

    tmp_snapshot_header.old_bitmap = alloc_block_result;
    addr = diskaddr(alloc_block_result);
    memcpy(addr, diskaddr(1), BLKSIZE);
    flush_block(addr);

    printf_debug("Temporary snapshot old bitmap block flushed\n");
    
    if ((header_write_result = pure_file_write(tmp_snapshot_file, &tmp_snapshot_header, HEADERSIZE, HEADERPOS)) != HEADERSIZE) {
        printf_debug("Temporary snapshot cannot be inited\n");
        return header_write_result;
    } else {
        printf_debug("Temporary snapshot inited\n");
    }

    pure_file_flush(tmp_snapshot_file);

    *current_snapshot_file = (uint64_t) tmp_snapshot_file;

    printf_debug("Start snapshot cfg update after temporary snapshot created\n");

    struct Snapshot_config cfg;

    if((cfg_read_result = pure_file_read(to_file(snapshot_config_file), &cfg, sizeof(struct Snapshot_config), HEADERPOS)) != sizeof(struct Snapshot_config)) {
        printf_debug("Snapshot config cannot be readed\n");
        return cfg_read_result;
    } else {
        printf_debug("Snapshot config readed\n");
    }

    strcpy(cfg.current_snapshot_name, tmp_snapshot_file->f_name);

    if((cfg_write_result = pure_file_write(to_file(snapshot_config_file), &cfg, sizeof(struct Snapshot_config), HEADERPOS)) != sizeof(struct Snapshot_config)) {
        printf_debug("Snapshot config cannot be updated\n");
        return cfg_write_result;
    } else {
        printf_debug("Snapshot config updated\n");
    }

    pure_file_flush(to_file(snapshot_config_file));

    printf_debug("Temporary snapshot created\n");

    return 0;
}

int fs_create_snapshot(const char * comment, const char * name) {
    
    printf_debug("Start creating snapshot with name: %s; and comment: %s!\n", name, comment);

    int file_search_result, read_header_result, write_header_result;

    struct Snapshot_header new_snapshot_header;

    struct File *translated_root_snapshot_file,*snapshot_file_for_delete;
    translated_root_snapshot_file = to_file(root_snapshot_file);
    snapshot_file_for_delete = translated_root_snapshot_file;
    
    // проверить а нет ли уже снапшотов с таким именем
    if ((file_search_result = find_snapshot_file_by_name(name, translated_root_snapshot_file, &snapshot_file_for_delete)) != -E_NOT_FOUND) {
        printf_debug("Cannot create snapshot with name %s because it's already exist\n", name);
        cprintf("Snapshot with name %s already exist\n", name);
        return file_search_result;
    }

    struct File *new_snapshot_file = to_file(current_snapshot_file);

    strcpy(new_snapshot_file->f_name, name);

    printf_debug("New snapshot file name updated, now is %s\n", new_snapshot_file->f_name);

    if((read_header_result = pure_file_read(new_snapshot_file, &new_snapshot_header, HEADERSIZE, HEADERPOS)) != HEADERSIZE) {
        printf_debug("Cannot create new snapshot, because current snapshot header cannot be readed\n");
        return read_header_result;
    }

    printf_debug("New snapshot file header readed\n");

    strcpy(new_snapshot_header.comment, comment);

    new_snapshot_header.date = sys_gettime();

    if((write_header_result = pure_file_write(new_snapshot_file, &new_snapshot_header, HEADERSIZE, HEADERPOS)) != HEADERSIZE) {
        printf_debug("Cannot create new snapshot, because current snapshot header cannot be updated\n");
        return write_header_result;
    }

    printf_debug("New snapshot file header was written\n");

    // rename files for tmp snapshots

    printf_debug("Start renaming files from deleted snapshot\n");
    
    int counter = 0;
    struct File *buffer_snapshoted_file;
    while(new_snapshot_header.modified_files[counter] != 0) {
    
        buffer_snapshoted_file = (struct File *) new_snapshot_header.modified_files[counter];

        printf_debug("Updating file name for %s\n", buffer_snapshoted_file->f_name);

        // переименновываем файлы временного снэпшота в постоянный
        char *pseparator = strfind(buffer_snapshoted_file->f_name, SNAPFILESEP[0]);
        ++pseparator;
        *pseparator = '\0';

        strcat(buffer_snapshoted_file->f_name, new_snapshot_file->f_name);

        printf_debug("Updated file name for %s\n", buffer_snapshoted_file->f_name);

        ++counter;
    }
    
    printf_debug("Deleted snapshot file name updated: %s\n", snapshot_file_for_delete->f_name);

    file_flush(new_snapshot_file);

    fs_create_tmp_snapshot();

    return 0;
}

int
fs_accept_snapshot(const char *name) {

    printf_debug("Try to accept snapshot with name %s\n", name);

    int file_search_result;//, header_read_result, header_write_result;
    
    struct File *translated_root_snapshot_file,*snapshot_file_for_accept;

    // here we should find snapshot by name

    translated_root_snapshot_file = to_file(root_snapshot_file);

    snapshot_file_for_accept = translated_root_snapshot_file;

    if ((file_search_result = find_snapshot_file_by_name(name, translated_root_snapshot_file, &snapshot_file_for_accept)) == -E_NOT_FOUND) {
        cprintf("Snapshot with name %s does not exist\n", name);
        return file_search_result;
    } else if(file_search_result != 0) {
        cprintf("Unknown error during seraching snapshot with name %s\n", name);
        return file_search_result;
    }

    printf_debug("Found snapshot with name %s\n", snapshot_file_for_accept->f_name);

    // delete tmp snapshot (and all files)

    delete_tmp_snapshot();

    struct File *last_snapshot_file = to_file(current_snapshot_file);

    if (last_snapshot_file != to_file(root_snapshot_file)) {
        delete_created_files_to_root(last_snapshot_file);
    }
    
    // now create files

    // if exist must make it current (create all files that should be create, delete all that should not be created)
    // deleting files should be done by undo all creation too root 
    // creation should be done by creation all files from root to new current snapshot
    // create tmp snapshot

    // struct Snapshot_header snapshot_for_accept_header;

    // struct File *snapshot_for_accept_file;

    // struct File *snapshots_before_accepted_file;

    // uint8_t *virt_address;

    // uint32_t address;
    // uint8_t value;
    // off_t offset = sizeof(struct Snapshot_header);

    // if (*old_current_snapshot_file) {
    //     snapshot_for_accept_file = (struct File *)(*old_current_snapshot_file);
    // } else {
    //     if ((*extra_snapshot_file) != 0) {
    //         *old_current_snapshot_file = *extra_snapshot_file;
    //         *extra_snapshot_file = 0;
    //         flush_block(super);
    //         snapshot_for_accept_file = (struct File *)(*old_current_snapshot_file);
    //     } else {
    //         printf_debug("No snapshot created for accept\n");
    //         return 0;
    //     }
    // }
  
    // //ищем файл снапшота по названию

    // bool not_found_end_from_root = true;

    // while (strcmp(snapshot_for_accept_file->f_name, name) != 0) {

    //     file_read(snapshot_for_accept_file, &snapshot_for_accept_header, sizeof(struct Snapshot_header), HEADERPOS);
    
    //     if (snapshot_for_accept_header.prev_snapshot != 0 && not_found_end_from_root) {
    //         snapshot_for_accept_file = (struct File *)snapshot_for_accept_header.prev_snapshot;
    //     } else {
            
    //         if (not_found_end_from_root) {
    //             snapshot_for_accept_file = (struct File *)(*old_current_snapshot_file);
    //             file_read(snapshot_for_accept_file, &snapshot_for_accept_header, sizeof(struct Snapshot_header), HEADERPOS);
    //             not_found_end_from_root = false;
    //         }
    
    //         // if (snapshot_for_accept_header.next_snapshot != 0) {
    //         //     snapshot_for_accept_file = (struct File *)snapshot_for_accept_header.next_snapshot;
    //         // } else {
    //         //     printf_debug("There is no snapshot with name %s\n", name);
    //             return 0;
    //         // }

    //     }
    // }

    // // Читаем заголовок найденного снапшота
    // file_read(snapshot_for_accept_file, &snapshot_for_accept_header, sizeof(struct Snapshot_header), HEADERPOS);

    // // если снепшот не корневой
    // if (snapshot_for_accept_header.prev_snapshot != 0) {

    //     //сливаем все снапшоты до применяемого в один (меняем ссылку на предыдущий в применяемом на ноль)
    //     snapshots_before_accepted_file = (struct File *)snapshot_for_accept_header.prev_snapshot;
    //     struct Snapshot_header snapshot_before_accept_header;

    //     // сохраняем заголовок снапшота который можеты быть исправленн
    //     pure_file_read(snapshots_before_accepted_file, &snapshot_before_accept_header, sizeof(struct Snapshot_header), HEADERPOS);

    //     while (concat_snapshot(snapshots_before_accepted_file)) {}

    //     // востанавливаем заголовок
    //     pure_file_write(snapshots_before_accepted_file, &snapshot_before_accept_header, sizeof(struct Snapshot_header), HEADERPOS);

    //     // пишем все данные из конкатенированного снапшота в текущий    
    //     while (file_read(snapshots_before_accepted_file, &address, ADDRESSSIZE, offset) == ADDRESSSIZE) {
    //         file_read(snapshots_before_accepted_file, &value, VALUESIZE, offset + ADDRESSSIZE);
    //         virt_address = (uint8_t *)((uint64_t)address + DISKMAP);
    //         *virt_address = value;
    //         offset += FIELDSIZE;
    //     }

    //     // может не надо
    //     //fs_delete_snapshot(snapshots_before_accepted_file->f_name);
    
    // }
    // fs_sync();

    // *extra_snapshot_file = *old_current_snapshot_file;
    // *old_current_snapshot_file = 0;
    // flush_block(super);

    return 1;
}

int
delete_created_files_to_root(struct File *snapshot_file) {
    int read_header_result;

    struct File *real_file; 

    struct Snapshot_header snapshot_header;

    if((read_header_result = pure_file_read(snapshot_file, &snapshot_header, HEADERSIZE, HEADERPOS)) != HEADERSIZE) {
        printf_debug("Cannot read header of temporary snapshot\n");
        return read_header_result;
    }

    int counter = 0;
    while(snapshot_header.created_files[counter] != 0) {
        real_file = (struct File *) snapshot_header.created_files[counter];

        pure_file_set_size(real_file, 0);
        memset(real_file, 0, sizeof(struct File));

        ++counter;
    }

    if (snapshot_header.prev_snapshot != to_file(root_snapshot_file)) {
        return delete_created_files_to_root((struct File *) snapshot_header.prev_snapshot);
    }
    
    return 0;
}

int
delete_tmp_snapshot() {
    // here tmp snapshot should be deleted (really deleted, not just flag update)
    printf_debug("Start deleting temporary snapshot\n");

    int read_header_result;

    struct File *snapshoted_file, *real_file; 

    struct Snapshot_header tmp_snapshot_header;

    struct File *tmp_snapshot_file = to_file(current_snapshot_file);

    if((read_header_result = pure_file_read(tmp_snapshot_file, &tmp_snapshot_header, HEADERSIZE, HEADERPOS)) != HEADERSIZE) {
        printf_debug("Cannot read header of temporary snapshot\n");
        return read_header_result;
    }

    // удаляем все снэпшотированные файлы
    int counter = 0;
    while(tmp_snapshot_header.modified_files[counter] != 0) {
        snapshoted_file = (struct File *) tmp_snapshot_header.modified_files[counter];

        pure_file_set_size(snapshoted_file, 0);
        memset(snapshoted_file, 0, sizeof(struct File));

        ++counter;
    }

    int counter = 0;
    while(tmp_snapshot_header.created_files[counter] != 0) {
        real_file = (struct File *) tmp_snapshot_header.created_files[counter];

        pure_file_set_size(real_file, 0);
        memset(real_file, 0, sizeof(struct File));

        ++counter;
    }

    // todo should be changed to = 0 ???
    *current_snapshot_file = tmp_snapshot_header.prev_snapshot;

    pure_file_set_size(tmp_snapshot_file, 0);
    memset(tmp_snapshot_file, 0, sizeof(struct File));


    //надо ли этого сделать
    // *current_snapshot_file = 0;
}

//Удаляет снимок по имени.
int
fs_delete_snapshot(const char *name) {

    printf_debug("Start deleting snapshot with name %s\n", name);

    int file_search_result, header_read_result, header_write_result;
    
    struct File *translated_root_snapshot_file,*snapshot_file_for_delete;

    translated_root_snapshot_file = to_file(root_snapshot_file);

    snapshot_file_for_delete = translated_root_snapshot_file;

    if ((file_search_result = find_snapshot_file_by_name(name, translated_root_snapshot_file, &snapshot_file_for_delete)) == -E_NOT_FOUND) {
        cprintf("Snapshot with name %s does not exist\n", name);
        return file_search_result;
    } else if(file_search_result != 0) {
        cprintf("Unknown error during seraching snapshot with name %s\n", name);
        return file_search_result;
    }

    printf_debug("Found snapshot with name %s\n", snapshot_file_for_delete->f_name);

    struct Snapshot_header header_for_delete;

    if((header_read_result = pure_file_read(snapshot_file_for_delete, &header_for_delete, HEADERSIZE, HEADERPOS)) != HEADERSIZE) {
        printf_debug("Snapshot's %s header that must be deleted cannot be readed\n", snapshot_file_for_delete->f_name);
        return header_read_result;
    }

    header_for_delete.is_deleted = true;

    if ((header_write_result = pure_file_write(snapshot_file_for_delete, &header_for_delete, HEADERSIZE, HEADERPOS)) != HEADERSIZE) {
        printf_debug("Snapshot's %s header that must be deleted cannot be updated\n", snapshot_file_for_delete->f_name);
        return header_write_result;
    }

    printf_debug("Deleted snapshot %s header updated\n", snapshot_file_for_delete->f_name);

    char time_stamp_int_string[MAXNAMELEN];
    
    if (itoa(header_for_delete.date, time_stamp_int_string, MAXNAMELEN, 10) != 0) {
        printf_debug("Cannot convert timestamp to string: %d\n", header_for_delete.date);
        return -E_INVAL;
    }

    strcat(snapshot_file_for_delete->f_name, time_stamp_int_string);

    printf_debug("Start renaming files from deleted snapshot\n");

    int counter = 0;
    struct File *buffer_snapshoted_file;
    while(header_for_delete.modified_files[counter] != 0) {
    
        buffer_snapshoted_file = (struct File *) header_for_delete.modified_files[counter];

        printf_debug("Updating file name for %s\n", buffer_snapshoted_file->f_name);

        strcat(buffer_snapshoted_file->f_name, time_stamp_int_string);

        printf_debug("Updated file name for %s\n", buffer_snapshoted_file->f_name);

        ++counter;
    }
    
    printf_debug("Deleted snapshot file name updated: %s\n", snapshot_file_for_delete->f_name);

    pure_file_flush(snapshot_file_for_delete);
    
    return 0;
}

int
find_snapshot_file_by_name(const char *name, struct File *root_snapshot_file, struct File **psnapshot_file) {

    struct File *buffer_snapshot_file;
    struct Snapshot_header snapshot_header, buffer_snapshot_header;

    int header_read_result, internal_search_result;

    if((header_read_result = pure_file_read(root_snapshot_file, &snapshot_header, HEADERSIZE, HEADERPOS)) != HEADERSIZE) {
        printf_debug("Cannot find snapshot by name, because cannot read header for snapshot %s\n", root_snapshot_file->f_name);
        return header_read_result;
    }

    int counter = 0;

    while(snapshot_header.next_snapshot[counter] != 0) {
        buffer_snapshot_file = (struct File *) snapshot_header.next_snapshot[counter];

        if((header_read_result = pure_file_read(buffer_snapshot_file, &buffer_snapshot_header, HEADERSIZE, HEADERPOS)) != HEADERSIZE) {
            printf_debug("Cannot find snapshot by name, because cannot read header for snapshot %s\n", buffer_snapshot_file->f_name);
            return header_read_result;
        }

        if (!buffer_snapshot_header.is_deleted && 
                strcmp(buffer_snapshot_file->f_name, name) == 0 && 
                buffer_snapshot_file != to_file(current_snapshot_file)) {

                    *psnapshot_file = buffer_snapshot_file;

                    return 0;
        } else if ((internal_search_result = find_snapshot_file_by_name(name, buffer_snapshot_file, psnapshot_file)) == 0){
            return 0;
        }
        
        ++counter;
    }

    return -E_NOT_FOUND;
}


//Список снимков
int
fs_print_snapshot_list() {
  
    struct File *snapshot_file;

    snapshot_file = to_file(root_snapshot_file);

    struct Snapshot_header root_header;
        
    pure_file_read(snapshot_file, &root_header, sizeof(struct Snapshot_header), HEADERPOS);
    cprintf("______________________________________\n");
    cprintf("\n\n\n");

    if(!internal_print_snapshot_list(snapshot_file, root_header)) {
        printf_debug("Try to print empty snpashots list\n");
        cprintf("There is no snapshots right now\n");
        return 0;
    }

    return 1;
}

bool
internal_print_snapshot_list(struct File *snap, struct Snapshot_header header) {
  
    printf_debug("Internal print for snapshot with name %s\n", snap->f_name);

    bool is_any_snapshots_printed = false;

    struct Snapshot_header next_snapshot_header;

    struct File *next_snapshot_file;

    struct tm time;

    // todo:: change back before pr
    if (!header.is_deleted) {//&& snap != to_file(current_snapshot_file)) { 
        is_any_snapshots_printed = true;

        mktime(header.date, &time);

        cprintf("   Name: %s\n", snap->f_name);
        cprintf("Comment: %s\n", header.comment);
        cprintf("   Time: %d/%d/%d %d:%d:%d\n", time.tm_mday, time.tm_mon+1, time.tm_year+1900, (time.tm_hour+3)%24, time.tm_min-2, time.tm_sec);
  
        cprintf("_____________________________________________\n");
        cprintf("\n\n\n");
    }

    for(int next_snap_index = 0; next_snap_index < MAXBRANCHES; ++next_snap_index) {
        if (header.next_snapshot[next_snap_index] != 0) {
            next_snapshot_file = (struct File *)(header.next_snapshot[next_snap_index]);
            if(pure_file_read(next_snapshot_file, &next_snapshot_header, HEADERSIZE, HEADERPOS) != HEADERSIZE) {
                printf_debug("Snapshot header for %s cannot be readed\n", next_snapshot_file->f_name)
            } else {
                is_any_snapshots_printed |= internal_print_snapshot_list(next_snapshot_file, next_snapshot_header);
            }
        }
    }

    return is_any_snapshots_printed;
}

/************************ functions end ************************************************************/

/********************** snapshot end ***************************************************************/






/********************** df start *******************************************************************/

int 
df_count_free_blocks() {
    int free_blocks = 0;

    for (blockno_t blockno = 1; blockno < super->s_nblocks; blockno++) {
        if (block_is_free(blockno)) {
            free_blocks++;
        }
    }

    printf_debug("Total amount of blocks in super %d; amount of free blocks: %d\n", super->s_nblocks, free_blocks);

    return free_blocks;
}

int 
df_count_busy_blocks() {
    int busy_blocks = 0;

    for (blockno_t blockno = 1; blockno < super->s_nblocks; blockno++) {
        if (!block_is_free(blockno)) {
            busy_blocks++;
        }
    }

    printf_debug("Total amount of blocks in super %d; amount of busy blocks: %d\n", super->s_nblocks, busy_blocks);

    return busy_blocks;
}

int
df_free_bytes() {
    //debug_print_bitmap();

    int free_blocks = df_count_free_blocks();

    printf_debug("Block size: %lld; free space deep %lld\n", BLKSIZE , free_blocks * BLKSIZE);

    return free_blocks * BLKSIZE;
}

int
df_busy_bytes() {
    //debug_print_bitmap();

    int busy_blocks = df_count_busy_blocks();

    printf_debug("Block size: %lld; busy space deep %lld\n", BLKSIZE , busy_blocks * BLKSIZE);

    return busy_blocks * BLKSIZE;
}
/********************** df end *********************************************************************/