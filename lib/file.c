#include <inc/fs.h>
#include <inc/string.h>
#include <inc/lib.h>

union Fsipc fsipcbuf __attribute__((aligned(PAGE_SIZE)));

/* Send an inter-environment request to the file server, and wait for
 * a reply.  The request body should be in fsipcbuf, and parts of the
 * response may be written back to fsipcbuf.
 * type: request code, passed as the simple integer IPC value.
 * dstva: virtual address at which to receive reply page, 0 if none.
 * Returns result from the file server. */
static int
fsipc(unsigned type, void *dstva) {
    static envid_t fsenv;

    if (!fsenv) fsenv = ipc_find_env(ENV_TYPE_FS);

    static_assert(sizeof(fsipcbuf) == PAGE_SIZE, "Invalid fsipcbuf size");

    if (debug) {
        cprintf("[%08x] fsipc %d %08x\n",
                thisenv->env_id, type, *(uint32_t *)&fsipcbuf);
    }

    ipc_send(fsenv, type, &fsipcbuf, PAGE_SIZE, PROT_RW);
    size_t maxsz = PAGE_SIZE;
    return ipc_recv(NULL, dstva, &maxsz, NULL);
}

static int devfile_flush(struct Fd *fd);
static ssize_t devfile_read(struct Fd *fd, void *buf, size_t n);
static ssize_t devfile_write(struct Fd *fd, const void *buf, size_t n);
static int devfile_stat(struct Fd *fd, struct Stat *stat);
static int devfile_trunc(struct Fd *fd, off_t newsize);

static int devfile_create_snapshot(char *comment, char *name);
static int devfile_print_snapshot_list();
static int devfile_accept_snapshot(char *name);
static int devfile_delete_snapshot(char *name);

struct Dev devfile = {
        .dev_id = 'f',
        .dev_name = "file",
        .dev_read = devfile_read,
        .dev_close = devfile_flush,
        .dev_stat = devfile_stat,
        .dev_write = devfile_write,
        .dev_trunc = devfile_trunc,
        .dev_sh_create = devfile_create_snapshot,
        .dev_sh_print = devfile_print_snapshot_list,
        .dev_sh_accept = devfile_accept_snapshot,
        .dev_sh_delete = devfile_delete_snapshot};



/* Open a file (or directory).
 *
 * Returns:
 *  The file descriptor index on success
 *  -E_BAD_PATH if the path is too long (>= MAXPATHLEN)
 *  < 0 for other errors. */
int
open(const char *path, int mode) {
    /* Find an unused file descriptor page using fd_alloc.
     * Then send a file-open request to the file server.
     * Include 'path' and 'omode' in request,
     * and map the returned file descriptor page
     * at the appropriate fd address.
     * FSREQ_OPEN returns 0 on success, < 0 on failure.
     *
     * (fd_alloc does not allocate a page, it just returns an
     * unused fd address.  Do you need to allocate a page?)
     *
     * Return the file descriptor index.
     * If any step after fd_alloc fails, use fd_close to free the
     * file descriptor. */

    int res;
    struct Fd *fd;

    if (strlen(path) >= MAXPATHLEN)
        return -E_BAD_PATH;


    if ((res = fd_alloc(&fd)) < 0) return res;


    strcpy(fsipcbuf.open.req_path, path);
    fsipcbuf.open.req_omode = mode;

    if ((res = fsipc(FSREQ_OPEN, fd)) < 0) {
        fd_close(fd, 0);
        return res;
    }

    return fd2num(fd);
}

/* Flush the file descriptor.  After this the fileid is invalid.
 *
 * This function is called by fd_close.  fd_close will take care of
 * unmapping the FD page from this environment.  Since the server uses
 * the reference counts on the FD pages to detect which files are
 * open, unmapping it is enough to free up server-side resources.
 * Other than that, we just have to make sure our changes are flushed
 * to disk. */
static int
devfile_flush(struct Fd *fd) {
    fsipcbuf.flush.req_fileid = fd->fd_file.id;
    return fsipc(FSREQ_FLUSH, NULL);
}

/* Read at most 'n' bytes from 'fd' at the current position into 'buf'.
 *
 * Returns:
 *  The number of bytes successfully read.
 *  < 0 on error. */
static ssize_t
devfile_read(struct Fd *fd, void *buf, size_t n) {
    /* Make an FSREQ_READ request to the file system server after
   * filling fsipcbuf.read with the request arguments.  The
   * bytes read will be written back to fsipcbuf by the file
   * system server. */

    // LAB 10: Your code here:
    if (!fd || !buf) {
        return E_INVAL;
    }

    size_t totalRead = 0;
    while (n) {
        size_t minSize = MIN(n, sizeof(fsipcbuf.readRet.ret_buf));
        fsipcbuf.read.req_fileid = fd->fd_file.id;
        fsipcbuf.read.req_n      = minSize;

        int read = fsipc(FSREQ_READ, NULL);
        if (read <= 0) {
            return read ? read : totalRead;
        }

        //cprintf("read %d expected read %zu max read %lu read buffer size %zu return buffer size %zu\n",read,minSize,n,sizeof(fsipcbuf.readRet.ret_buf),sizeof(buf));
        
        memmove(buf, fsipcbuf.readRet.ret_buf, read);

        buf += read;
        n -= read;
        totalRead += read;
  }

  return totalRead;
}

/* Write at most 'n' bytes from 'buf' to 'fd' at the current seek position.
 *
 * Returns:
 *   The number of bytes successfully written.
 *   < 0 on error. */
static ssize_t
devfile_write(struct Fd *fd, const void *buf, size_t n) {
    /* Make an FSREQ_WRITE request to the file system server.  Be
   * careful: fsipcbuf.write.req_buf is only so large, but
   * remember that write is always allowed to write *fewer*
   * bytes than requested. */
    // LAB 10: Your code here:
    if (!fd || !buf) {
        return -E_INVAL;
    }

    int totalWrite = 0;
    while (n) {
        size_t minSize = MIN(n, sizeof(fsipcbuf.write.req_buf));

        memmove(fsipcbuf.write.req_buf, buf, minSize);

        fsipcbuf.write.req_fileid = fd->fd_file.id;
        fsipcbuf.write.req_n = minSize;

        int write = fsipc(FSREQ_WRITE, NULL);
        if (write < 0) {
            return write;
        }

        buf += write;
        n -= write;
        totalWrite += write;
    }

    return totalWrite;
}

/* Get file information */
static int
devfile_stat(struct Fd *fd, struct Stat *st) {
    fsipcbuf.stat.req_fileid = fd->fd_file.id;
    int res = fsipc(FSREQ_STAT, NULL);
    if (res < 0) return res;

    strcpy(st->st_name, fsipcbuf.statRet.ret_name);
    st->st_size = fsipcbuf.statRet.ret_size;
    st->st_isdir = fsipcbuf.statRet.ret_isdir;

    return 0;
}

/* Truncate or extend an open file to 'size' bytes */
static int
devfile_trunc(struct Fd *fd, off_t newsize) {
    fsipcbuf.set_size.req_fileid = fd->fd_file.id;
    fsipcbuf.set_size.req_size = newsize;

    return fsipc(FSREQ_SET_SIZE, NULL);
}

static int
devfile_create_snapshot(char *comment, char *name) {
    if (!comment || !name) {
        return -E_INVAL;
    }

    if (strlen(comment) > MAX_SH_LENGTH || strlen(name) > MAX_SH_LENGTH) {
        return -E_INVAL;
    }
    
    strcpy(comment,fsipcbuf.snapshot_create.comment);
    strcpy(name,fsipcbuf.snapshot_create.name);

    int res = fsipc(FSREQ_SH_CREATE, NULL);
    if (res < 0) return res;

    return 0;
}

static int 
devfile_print_snapshot_list() {
    int res = fsipc(FSREQ_SH_PRINT, NULL);
    if (res < 0) return res;

    return 0;
}

static int
devfile_accept_snapshot(char *name) {
    if (!name) {
        return -E_INVAL;
    }

    if (strlen(name) > MAX_SH_LENGTH) {
        return -E_INVAL;
    }
    
    strcpy(name, fsipcbuf.snapshot_accept.name);

    int res = fsipc(FSREQ_SH_ACCEPT, NULL);
    if (res < 0) return res;

    return 0;
}

static int
devfile_delete_snapshot(char *name) {
    if (!name) {
        return -E_INVAL;
    }

    if (strlen(name) > MAX_SH_LENGTH) {
        return -E_INVAL;
    }
    
    strcpy(name, fsipcbuf.snapshot_delete.name);

    int res = fsipc(FSREQ_SH_DELETE, NULL);
    if (res < 0) return res;

    return 0;
}

/* Synchronize disk with buffer cache */
int
sync(void) {
    /* Ask the file server to update the disk
     * by writing any dirty blocks in the buffer cache. */

    return fsipc(FSREQ_SYNC, NULL);
}
