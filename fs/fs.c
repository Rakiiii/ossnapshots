#include <inc/string.h>
#include <inc/partition.h>
#include <inc/time.h>

#include "fs.h"

/* Superblock */
struct Super *super;
/* Bitmap blocks mapped in memory */
uint32_t *bitmap;

/********************************************************** snapshot region *****************/
uint64_t * current_snapshot_file = 0;
uint64_t * extra_snapshot_file = 0;

bool SNAPSHOT_DEBUG = true;

#define printf_debug(M, ...) if (SNAPSHOT_DEBUG) cprintf("[%s:%d] Note: " M "\n",__FILE__, __LINE__,##__VA_ARGS__);


static int 
internal_print_snapshot_list(struct File *snap, struct Snapshot_header header);
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

    return 0;
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
fs_init(void) {
    static_assert(sizeof(struct File) == 256, "Unsupported file size");

    /* Find a JOS disk.  Use the second IDE disk (number 1) if availabl */
    if (ide_probe_disk1())
        ide_set_disk(1);
    else
        ide_set_disk(0);
    bc_init();

    /* Set "super" to point to the super block. */
    super = diskaddr(1);

    current_snapshot_file = (uint64_t *) (super + 1);
    extra_snapshot_file = (uint64_t *) (current_snapshot_file + 1);
    if (*current_snapshot_file != 0) {
        struct Snapshot_header header;
        file_read((struct File *)*current_snapshot_file, &header, sizeof(header),0);
    }

    check_super();

    /* Set "bitmap" to the beginning of the first bitmap block. */
    bitmap = diskaddr(2);

    check_bitmap();
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
    char name[MAXNAMELEN];
    int res;
    struct File *dir, *filp;

    if (!(res = walk_path(path, &dir, &filp, name))) return -E_FILE_EXISTS;
    if (res != -E_NOT_FOUND || dir == 0) return res;
    if ((res = dir_alloc_file(dir, &filp)) < 0) return res;

    strcpy(filp->f_name, name);
    *pf = filp;
    file_flush(dir);
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
    int r, bn;
    off_t pos;
    char *blk;

    if (*current_snapshot_file != 0 && find_in_snapshot_list(f)==0 && f->f_type != FTYPE_DIR) {
        return snapshot_file_read(f,buf,count,offset);
    }

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

    printf_debug("writing in file %s...\n",f->f_name);

    if (*current_snapshot_file != 0 && find_in_snapshot_list(f)==0) { 
        if (find_in_snapshot_list(f)==0) {
            return snapshot_file_write(f,buf,count,offset);
        }
    }

    int r, bn;
    off_t pos;
    char *blk;

    // Extend file if necessary
    if (offset + count > f->f_size) {
        if ((r = file_set_size(f, offset + count)) < 0) {
            return r;
        }
    }

    for (pos = offset; pos < offset + count;) {

        if ((r = file_get_block(f, pos / BLKSIZE, &blk)) < 0) {
            return r;
        }

        bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
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
    printf_debug("new size %d for file %s\n",(int)newsize,f->f_name);


    // если файл в снэпшоте
    if ((current_snapshot_file != NULL) && (*current_snapshot_file != 0) && find_in_snapshot_list(f)==0) {
        char *addr;
        off_t buf_offset;
        uint32_t disk_addr;
        struct File *snap = (struct File *)(*current_snapshot_file);
        addr = (char *)&f->f_size;
        int r, snap_offset = snap->f_size;

        for (int i=0; i<sizeof(newsize);i++) {
            addr += i;
      
            // ищем адрес
            if ( (r = find_in_snapshot(snap, (uint64_t)addr, &buf_offset)) == 1) {
                file_write(snap, (char *)&newsize + i, 1, buf_offset + sizeof(uint32_t)); 
            } else {
                disk_addr = (uint32_t)((uint64_t)addr - DISKMAP);

                file_write(snap, &disk_addr, sizeof(uint32_t), snap_offset);
                snap_offset+=sizeof(uint32_t);
        
                file_write(snap, (char *)&newsize + i, 1, snap_offset);
                snap_offset++;
            }
        }
    } else {
        // если файл не в снэпшоте
        if (f->f_size > newsize) {
            file_truncate_blocks(f, newsize);
        }

        f->f_size = newsize;
        flush_block(f);
    }
    printf_debug("OUT FROM SET SIZE\n");
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
  int i;
  uint32_t *pdiskbno;

  if (*current_snapshot_file != 0) {  
    if (find_in_snapshot_list(f)==1) {
      for (i = 0; i < (f->f_size + BLKSIZE - 1) / BLKSIZE; i++) {
        
        if (file_block_walk(f, i, &pdiskbno, 0) < 0 ||
            pdiskbno == NULL || *pdiskbno == 0)
          continue;

        flush_block(diskaddr(*pdiskbno));
      }

      flush_block(f);
      
      if (f->f_indirect) {
        flush_block(diskaddr(f->f_indirect));
      }
    }
    else
      file_flush((struct File *)*current_snapshot_file);
  }
  else
  {
    for (i = 0; i < (f->f_size + BLKSIZE - 1) / BLKSIZE; i++) {
        if (file_block_walk(f, i, &pdiskbno, 0) < 0 ||
            pdiskbno == NULL || *pdiskbno == 0)
          continue;
        flush_block(diskaddr(*pdiskbno));
      }
      flush_block(f);
      if (f->f_indirect)
      {
        flush_block(diskaddr(f->f_indirect));
      }
  }
}
// void
// file_flush(struct File *f) {
//     blockno_t *pdiskbno;

//     for (blockno_t i = 0; i < CEILDIV(f->f_size, BLKSIZE); i++) {
//         if (file_block_walk(f, i, &pdiskbno, 0) < 0 ||
//             pdiskbno == NULL || *pdiskbno == 0)
//             continue;
//         flush_block(diskaddr(*pdiskbno));
//     }
//     if (f->f_indirect)
//         flush_block(diskaddr(f->f_indirect));
//     flush_block(f);
// }

/* Sync the entire file system.  A big hammer. */
void
fs_sync(void) {
    for (int i = 1; i < super->s_nblocks; i++) {
        flush_block(diskaddr(i));
    }
}



/********************** snapshot ****************************************************************/

/********************** utils start ******************************************************************/

//Ищет данный адрес по всем снимкам, начиная с самого последнего.
int find_in_snapshot(struct File * snapshot,uint64_t my_addr, off_t * offset) {
    int r;
    uint32_t disk_addr = (uint32_t)(my_addr - DISKMAP);

    char buf[SNAP_BUF_SIZE];
    char * buffer;
  
    for (int pos=sizeof(struct Snapshot_header);pos<snapshot->f_size;pos+=SNAP_BUF_SIZE) {

        if ((r=file_read(snapshot,buf,SNAP_BUF_SIZE,pos))<0) {
            return r;
        }

        buffer = buf;

        for (int i=0;i<r/5;i++) {
            if (*(uint32_t *)buffer == disk_addr) {
                *offset = pos + i*5;
                return 1;
            } else {
                buffer+=5;
            }
        }
    }

    return 0;
}

//Ищет реальный размер файла по снэпшотам.
off_t snapshot_find_size(struct File * f) {

    char * addr = (char *)&f->f_size;
    struct File * snapshot = (struct File *)(*current_snapshot_file);
    struct File * buf_snapshot;
    int r;
    int my_number;
    char * number = (char *)&my_number;
    off_t buf_offset;

    for (int i=0;i<sizeof(uint32_t);i++) {
        buf_snapshot = snapshot;

        while(buf_snapshot!=NULL) {
            if ( (r = find_in_snapshot(buf_snapshot, (uint64_t)addr, &buf_offset)) == 1) {
                file_read(buf_snapshot, number+i, 1, buf_offset + sizeof(uint32_t));
                break;  
            } else if (!r) {
                if (
                    (r=file_read(
                        buf_snapshot,
                        &buf_snapshot,
                        sizeof(struct File *),
                        sizeof(struct Snapshot_header)-sizeof(uint64_t))
                        )<0
                    ) {
                        return r;
                    }
            } else { 
                return r;
            }
        }

        if (buf_snapshot==NULL) {
            number[i] = *addr;
        }
    
        addr++;
    }

    return my_number;
}

//Ищет данный файл в списке снимков. 
//Возвращает 1 в случае успеха.
int find_in_snapshot_list(struct File * f) {
    int r;
    struct File * next_snapshot = (struct File *)*current_snapshot_file;

    while (next_snapshot!=NULL) {

        if (f==next_snapshot) {
            return 1;
        } else {

            if ((r=file_read(
                        (struct File *)next_snapshot,
                        &next_snapshot,
                        sizeof(struct File *),
                        sizeof(struct Snapshot_header)-sizeof(uint64_t)
                        )
                    ) < 0 ) {

                    return r;
                }
    }
  }

  return 0;
}

/********************** utils end   ******************************************************************/

/********************** logic start ******************************************************************/

//Читает файл сначала по всем снимкам, далее, если нужный адрес не найден, читает непосредственно
//из файловой системы. И так с каждым адресом.
int snapshot_file_read(struct File *f, void *buf, size_t count, off_t offset) {
    int r, bn;
    off_t pos;

    char * addr;
    off_t buf_offset;

    if (offset >= snapshot_find_size(f)) {
        return 0;
    }

    count = MIN(count, snapshot_find_size(f) - offset);

    struct File * snapshot = (struct File *) *current_snapshot_file; 
    struct File * buf_snapshot;
    for (pos = offset; pos < offset + count;) {

        if ((r = file_get_block(f, pos / BLKSIZE, &addr)) < 0) {
            return r;
        }

        addr = addr + pos % BLKSIZE; 
        bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);

        for (int i=0;i<bn;i++) {
            buf_snapshot = snapshot;

            while(buf_snapshot!=0) {
                
                if ( (r = find_in_snapshot(buf_snapshot, (uint64_t)addr, &buf_offset)) == 1) {
                    file_read(buf_snapshot, buf, 1, buf_offset + sizeof(uint32_t));
                    break;  
                } else if (!r) {
                    if (
                        (r=file_read(
                            buf_snapshot,
                            &buf_snapshot,
                            sizeof(struct File *),
                            sizeof(struct Snapshot_header)-sizeof(uint64_t))
                            )<0
                        ) {
                            return r;
                        }
                } else { 
                    return r;
                }
            }

            if (!buf_snapshot) {
                *(char *)buf = *addr;
            }

            addr++;
            buf++;
        }

        pos+=bn;
    }
  
    return count;
}

//Вызывается вместо file_write и записывает все изменения в последний снимок.
int snapshot_file_write(struct File *f, const void *buf, size_t count, off_t offset) {
    int r, bn;
    char * addr;
    off_t pos;
    off_t new_size;

    uint32_t disk_addr;

    struct File * snapshot = (struct File *) *current_snapshot_file; 
    int snap_offset = snapshot->f_size;

    off_t buf_offset;

    // try to fix
    if (offset + count > snapshot_find_size(f)) {
        addr = (char *)&f->f_size;
        new_size = offset + count;

        for (int i=0; i<sizeof(new_size);i++) {
            addr += i;

            // ищем адрес 
            if ( (r = find_in_snapshot(snapshot, (uint64_t)addr, &buf_offset)) == 1) {
                file_write(snapshot, (char *)&new_size + i, 1, buf_offset + sizeof(uint32_t)); 
            } else {
                disk_addr = (uint32_t)((uint64_t)addr - DISKMAP);

                file_write(snapshot, &disk_addr, sizeof(uint32_t), snap_offset);
                snap_offset+=sizeof(uint32_t);
        
                file_write(snapshot, (char *)&new_size + i, 1, snap_offset);
                snap_offset++;
            }

        }
    }

    for (pos = offset; pos < offset + count;) {

        if ((r = file_get_block(f, pos / BLKSIZE, &addr)) < 0) {
            return r;
        }

        addr = addr + pos % BLKSIZE; 
        bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);

        for (int i=0;i<bn;i++) {

            if ( (r = find_in_snapshot(snapshot,(uint64_t)addr, &buf_offset)) == 1) {
                file_write(snapshot, buf, 1, buf_offset + sizeof(uint32_t));
            } else if (!r) {
                disk_addr = (uint32_t)((uint64_t)addr - DISKMAP);

                file_write(snapshot, &disk_addr, sizeof(uint32_t), snap_offset); 
                snap_offset+=sizeof(uint32_t);

                file_write(snapshot, buf, 1, snap_offset); 
                snap_offset ++;
            } else { 
                return r;
            }

            addr++;
            buf++;
        }
    
        pos += bn;
    }

  return count;
}

/********************** logic end   ******************************************************************/

/************************ functions start ************************************************************/
int fs_create_snapshot(const char * comment, const char * name) {
    
    printf_debug("Welcome to create_snapshot!\n");
    
    struct File * new_snapshot_file;
    int file_create_result, alloc_block_result;
    char * addr;
    struct Snapshot_header new_snapshot_header;

    // Создаем файл для хранения снепшота
    if ((file_create_result = file_create(name, &new_snapshot_file)) < 0) {
        printf_debug("File for snapshot cannot be created\n");
        return file_create_result;
    }

    // Если существует текущий снэпшот то флашим только его
    if (*current_snapshot_file != 0) {
        printf_debug("Snapshot already exist\n");
        file_flush((struct File *)*current_snapshot_file);
    } else {
        fs_sync();
    }

    // выделяем блок
    if ((alloc_block_result = alloc_block()) == 0) {
        printf_debug("Out of memory! snapshot cannot be created \n");
        return alloc_block_result;
    }
  
    new_snapshot_header.old_bitmap = alloc_block_result;
    addr = diskaddr(alloc_block_result);
    memcpy(addr, diskaddr(1), BLKSIZE);
    flush_block(addr);

    /*
        так как сделал free_block(r), чтобы old_bitmap был таким, как до создания снимка
        bitmap[r / 32] &= ~(1 << (r % 32));
        flush_block(&bitmap[r / 32]);
    */


    new_snapshot_header.prev_snapshot = *current_snapshot_file;
    strcpy(new_snapshot_header.comment, comment);
    new_snapshot_header.date = sys_gettime();

    *current_snapshot_file = (uint64_t) new_snapshot_file;
    file_write(new_snapshot_file, &new_snapshot_header, sizeof(struct Snapshot_header), 0);
    flush_block(current_snapshot_file);

    printf_debug(
        "old: %llx new: %llx \n",
            (unsigned long long)new_snapshot_header.prev_snapshot,
            (unsigned long long)*current_snapshot_file
            );

    printf_debug(
        "Snapshot created name: %s size:%d %d!\n",
            name, 
            new_snapshot_file->f_size, 
            (int)sizeof(struct Snapshot_header)
            );

    printf_debug( 
        "Comment: %d | date: %d | old_bitmap: %d | prev_snapshot: %d\n",
            (int)sizeof(new_snapshot_header.comment),
            (int)sizeof(new_snapshot_header.date),
            (int)sizeof(new_snapshot_header.old_bitmap),
            (int)sizeof(new_snapshot_header.prev_snapshot)
            );

  return 0;
}

//Применяет снимок по имени.
int
accept_snapshot(const char *name) {

    struct Snapshot_header new_header;
    struct File *snapshot_file;
    struct File *help_snap;
    uint8_t *virt_address;
    uint32_t address;
    uint8_t value;
    off_t offset = sizeof(struct Snapshot_header);

    if (*current_snapshot_file) {
        snapshot_file = (struct File *)(*current_snapshot_file);
    } else {
        printf_debug("error1 in accept_snapshot\n");
        return 0;
    }
  
    //ищем файл снапшота по названию
    while (strcmp(snapshot_file->f_name, name) != 0) {

        file_read(snapshot_file, &new_header, sizeof(struct Snapshot_header), 0);
    
        if (new_header.prev_snapshot != 0) {
            snapshot_file = (struct File *)new_header.prev_snapshot;
        } else {
            cprintf("There is no such snapshot\n");
            return 0;
        }
    }

    //сливаем все снапшоты до применяемого в один (меняем ссылку на предыдущий в применяемом на ноль)
    file_read(snapshot_file, &new_header, sizeof(struct Snapshot_header), 0);
    if (new_header.prev_snapshot != 0) {

        help_snap = (struct File *)new_header.prev_snapshot;

        while (merge_snapshot(help_snap)) {}

        //new_header.prev_snapshot = 0;
        //file_write(snap, &new_header, sizeof(struct Snapshot_header), 0);

        //применяем все изменения и приводим фс в актуальное состояние    
        while (file_read(help_snap, &address, 4, offset) == 4) {
            file_read(help_snap, &value, 1, offset + 4);
            virt_address = (uint8_t *)((uint64_t)address + DISKMAP);
            *virt_address = value;
            offset += 5;
        }

        delete_snapshot(help_snap->f_name);
    
    }
    fs_sync();

    *extra_snapshot_file = *current_snapshot_file;
    *current_snapshot_file = 0;
    flush_block(super);

    return 1;
}

//Удаляет снимок по имени.
int
delete_snapshot(const char *name) {

    char lastelem[MAXNAMELEN];
    int r;
    struct File *dir, *f;
    struct File *snap, *help_snap;
    struct Snapshot_header new_header, help_header;
  
    if (*current_snapshot_file) {
        snap = help_snap = (struct File *)(*current_snapshot_file);
    } else {
        printf_debug("No current snapshot file for delete\n");
        return 0;
    }

    while (strcmp(snap->f_name, name) != 0) {
        
        file_read(snap, &new_header, sizeof(struct Snapshot_header), 0);
        if (new_header.prev_snapshot != 0) {
            help_snap = snap;
            snap = (struct File *)new_header.prev_snapshot;
        } else {
            printf_debug("No snapshot for delete with name: %s\n", name);
            return 0;
        }
    }

    if (!strcmp(help_snap->f_name, name)) {
        file_read(help_snap, &new_header, sizeof(struct Snapshot_header), 0);
        *current_snapshot_file = new_header.prev_snapshot;
        flush_block(current_snapshot_file);
    } else {
        file_read(snap, &help_header, sizeof(struct Snapshot_header), 0);
        new_header.prev_snapshot = help_header.prev_snapshot;
        file_write(help_snap, &new_header, sizeof(struct Snapshot_header), 0);
    }
  

    file_set_size(snap, 0);
  
    if ((r = walk_path(name, &dir, &f, lastelem)) != 0) {
        printf_debug("There is no such file to delete\n");
        return 0;
    } else {
        if (snap != f) {
            printf_debug("Some how snapshot files are not the same\n");
            return 0;
        }
        
        /*for(int i = 0; i <= MAXNAMELEN - 1; i++)
        snap->f_name[i] = '\0';*/
        
        memset(snap, 0, sizeof(struct File));
    }
    
    fs_sync();

    return 1;
}


//Список снимков
int fs_print_snapshot_list() {
  
    struct File *snapshot_file;
    struct Snapshot_header header;
  
    if (!(*current_snapshot_file)) {
        printf_debug("Try to print snapshot without them\n");
        return 0;
    }
        
    snapshot_file = (struct File *)(*current_snapshot_file);
    file_read(snapshot_file, &header, sizeof(struct Snapshot_header), 0);
    cprintf("___________  _________________________\n");
    cprintf("           \\/\n");

    internal_print_snapshot_list(snapshot_file, header);

    return 1;
}

int internal_print_snapshot_list(struct File *snap, struct Snapshot_header header) {
  
  struct Snapshot_header help_header;
  struct File *help_snap;
  struct tm time;

  if (header.prev_snapshot != 0) {
    help_snap = (struct File *)(header.prev_snapshot);
    file_read(help_snap, &help_header, sizeof(struct Snapshot_header), 0);
    internal_print_snapshot_list(help_snap, help_header);
  }

  mktime(header.date, &time);

  cprintf("   Name: %s\n", snap->f_name);
  cprintf("Comment: %s\n", header.comment);
  cprintf("   Time: %d/%d/%d %d:%d:%d\n", time.tm_mday, time.tm_mon+1, time.tm_year+1900, (time.tm_hour+3)%24, time.tm_min-2, time.tm_sec);
  
  cprintf("_____________________________________________\n");
  cprintf("           \\/\n");

  return 1;
}