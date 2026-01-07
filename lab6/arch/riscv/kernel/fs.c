#include "fs.h"
#include "buf.h"
#include "defs.h"
#include "slub.h"
#include "task_manager.h"
#include "virtio.h"
#include "vm.h"
#include "mm.h"
#include "stdio.h"

// --------------------------------------------------
// ----------- read and write interface -------------

void disk_op(int blockno, uint8_t *data, bool write) {
    struct buf b;
    b.disk = 0;
    b.blockno = blockno;
    b.data = (uint8_t *)PHYSICAL_ADDR(data);
    virtio_disk_rw((struct buf *)(PHYSICAL_ADDR(&b)), write);
}

#define disk_read(blockno, data) disk_op((blockno), (data), 0)
#define disk_write(blockno, data) disk_op((blockno), (data), 1)

// -------------------------------------------------
// ------------------ your code --------------------

extern int strcmp(const char *a, const char *b);

char *strcpy(char *dst, const char *src) {
    char *os = dst;
    while ((*dst++ = *src++) != 0)
        ;
    return os;
}

int strlen(const char *s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}

struct sfs_fs *sfs_fs;

#define SFS_HASH_BITS 7
#define SFS_HASH_SIZE (1 << SFS_HASH_BITS)

static int sfs_hash(uint32_t ino) {
    return ino % SFS_HASH_SIZE;
}

int sfs_init() {
    // 1. 申请 sfs_fs 数据结构内存
    sfs_fs = (struct sfs_fs *)kmalloc(sizeof(struct sfs_fs));
    if (!sfs_fs) {
        printf("sfs_init: failed to allocate sfs_fs\n");
        return -1;
    }

    // 2. 读取 superblock (block 0)
    uint8_t *buf = (uint8_t *)kmalloc(4096);
    if (!buf) {
        printf("sfs_init: failed to allocate buffer\n");
        kfree(sfs_fs);
        return -1;
    }
    
    disk_read(0, buf);
    sfs_fs->super = *(struct sfs_super *)buf;
    kfree(buf);

    // 3. 检查 magic number
    if (sfs_fs->super.magic != SFS_MAGIC) {
        printf("sfs_init: invalid magic %x\n", sfs_fs->super.magic);
        kfree(sfs_fs);
        return -1;
    }
    printf("sfs_init: magic valid. blocks: %d, unused: %d\n", sfs_fs->super.blocks, sfs_fs->super.unused_blocks);

    // 4. 计算 freemap 需要的 block 数量并申请内存
    uint32_t bits_per_block = 4096 * 8;
    uint32_t freemap_blocks = (sfs_fs->super.blocks + bits_per_block - 1) / bits_per_block;
    
    sfs_fs->freemap = (uint8_t *)kmalloc(freemap_blocks * 4096);
    if (!sfs_fs->freemap) {
        printf("sfs_init: failed to allocate freemap\n");
        kfree(sfs_fs);
        return -1;
    }

    // 5. 读取 freemap (block 2 ~ 2 + freemap_blocks)
    for (int i = 0; i < freemap_blocks; i++) {
        disk_read(2 + i, sfs_fs->freemap + i * 4096);
    }

    // 6. 初始化 inode 链表
    INIT_LIST_HEAD(&sfs_fs->inode_list);
    
    // 7. 初始化 hash 表
    sfs_fs->hash_list = (struct list_head *)kmalloc(sizeof(struct list_head) * SFS_HASH_SIZE);
    if (!sfs_fs->hash_list) {
        printf("sfs_init: failed to allocate hash_list\n");
        kfree(sfs_fs->freemap);
        kfree(sfs_fs);
        return -1;
    }
    
    for (int i = 0; i < SFS_HASH_SIZE; i++) {
        INIT_LIST_HEAD(&sfs_fs->hash_list[i]);
    }

    sfs_fs->super_dirty = 0;

    printf("sfs_init: success!\n");
    return 0;
}

// Helper: Alloc block
int sfs_alloc_block() {
    int byte_len = (sfs_fs->super.blocks + 7) / 8;
    for (int i = 0; i < byte_len; i++) {
        if (sfs_fs->freemap[i] != 0xFF) {
            for (int j = 0; j < 8; j++) {
                if (!((sfs_fs->freemap[i] >> j) & 1)) {
                    sfs_fs->freemap[i] |= (1 << j);
                    sfs_fs->super.unused_blocks--;
                    sfs_fs->super_dirty = 1;
                    return i * 8 + j;
                }
            }
        }
    }
    return -1;
}

// Helper: Free block
void sfs_free_block(int blockno) {
    int idx = blockno / 8;
    int bit = blockno % 8;
    sfs_fs->freemap[idx] &= ~(1 << bit);
    sfs_fs->super.unused_blocks++;
    sfs_fs->super_dirty = 1;
}

// Helper: Get inode (load from disk or cache)
struct sfs_memory_block *sfs_get_inode(uint32_t ino) {
    int hash = sfs_hash(ino);
    struct list_head *pos;
    list_for_each(pos, &sfs_fs->hash_list[hash]) {
        // struct sfs_memory_block *mb = list_entry(pos, struct sfs_memory_block, inode_link);
        struct sfs_memory_block *mb = (struct sfs_memory_block *)((char *)pos - offsetof(struct sfs_memory_block, inode_link));
        if (mb->blockno == ino) {
            mb->reclaim_count++;
            return mb;
        }
    }

    struct sfs_memory_block *mb = (struct sfs_memory_block *)kmalloc(sizeof(struct sfs_memory_block));
    mb->blockno = ino;
    mb->is_inode = 1;
    mb->dirty = 0;
    mb->reclaim_count = 1;
    mb->block.din = (struct sfs_inode *)kmalloc(4096);
    disk_read(ino, (uint8_t *)mb->block.din);

    list_add(&mb->inode_link, &sfs_fs->hash_list[hash]);
    return mb;
}

// Helper: Sync inode
void sfs_sync_inode(struct sfs_memory_block *mb) {
    if (mb->dirty) {
        disk_write(mb->blockno, (uint8_t *)mb->block.din);
        mb->dirty = 0;
    }
}

// Helper: Put inode
void sfs_put_inode(struct sfs_memory_block *mb) {
    mb->reclaim_count--;
    if (mb->reclaim_count == 0) {
        sfs_sync_inode(mb);
        list_del(&mb->inode_link);
        kfree(mb->block.din);
        kfree(mb);
    }
}

// Helper: Get block ID of file, allocate if create=true
int sfs_get_block_id(struct sfs_memory_block *mb, uint32_t block_idx, bool create) {
    struct sfs_inode *inode = mb->block.din;
    if (block_idx < SFS_NDIRECT) {
        if (inode->direct[block_idx] == 0) {
            if (!create) return 0;
            int new_block = sfs_alloc_block();
            if (new_block < 0) return -1;
            inode->direct[block_idx] = new_block;
            inode->blocks++;
            mb->dirty = 1;
            
            // Clear new block
            uint8_t *buf = (uint8_t *)kmalloc(4096);
            memset(buf, 0, 4096);
            disk_write(new_block, buf);
            kfree(buf);
        }
        return inode->direct[block_idx];
    } else {
        if (inode->indirect == 0) {
            if (!create) return 0;
            int new_block = sfs_alloc_block();
            if (new_block < 0) return -1;
            inode->indirect = new_block;
            mb->dirty = 1;
            
            uint32_t *buf = (uint32_t *)kmalloc(4096);
            memset(buf, 0, 4096);
            disk_write(new_block, (uint8_t *)buf);
            kfree(buf);
        }
        
        uint32_t *indirect_block = (uint32_t *)kmalloc(4096);
        disk_read(inode->indirect, (uint8_t *)indirect_block);
        
        uint32_t idx = block_idx - SFS_NDIRECT;
        if (indirect_block[idx] == 0) {
            if (!create) {
                kfree(indirect_block);
                return 0;
            }
            int new_block = sfs_alloc_block();
            if (new_block < 0) {
                kfree(indirect_block);
                return -1;
            }
            indirect_block[idx] = new_block;
            disk_write(inode->indirect, (uint8_t *)indirect_block);
            inode->blocks++;
            
            // Clear new block
            uint8_t *buf = (uint8_t *)kmalloc(4096);
            memset(buf, 0, 4096);
            disk_write(new_block, buf);
            kfree(buf);
        }
        int ret = indirect_block[idx];
        kfree(indirect_block);
        return ret;
    }
}

// Helper: Add entry to directory
int sfs_dir_link(struct sfs_memory_block *dir, const char *filename, uint32_t ino) {
    struct sfs_inode *din = dir->block.din;
    if (din->type != SFS_DIRECTORY) return -1;

    // Find empty slot or append
    int n_entries = 4096 / sizeof(struct sfs_entry);
    
    for (int i = 0; i < SFS_NDIRECT + 1024; i++) { // Limit search
        int bno = sfs_get_block_id(dir, i, 1); // Create if needed
        if (bno < 0) return -1;
        
        char *buf = (char *)kmalloc(4096);
        disk_read(bno, (uint8_t *)buf);
        struct sfs_entry *entries = (struct sfs_entry *)buf;
        
        for (int j = 0; j < n_entries; j++) {
            if (strlen(entries[j].filename) == 0) {
                // Found empty slot
                entries[j].ino = ino;
                strcpy(entries[j].filename, filename);
                disk_write(bno, (uint8_t *)buf);
                kfree(buf);
                
                // Update dir size
                int entry_size = sizeof(struct sfs_entry);
                int new_size = (i * n_entries + j + 1) * entry_size;
                if (new_size > din->size) {
                    din->size = new_size;
                    dir->dirty = 1;
                }
                return 0;
            }
        }
        kfree(buf);
    }
    return -1;
}

// Helper: Create new inode
int sfs_create_inode(int type) {
    int ino = sfs_alloc_block();
    if (ino < 0) return -1;
    
    struct sfs_memory_block *mb = sfs_get_inode(ino);
    memset(mb->block.din, 0, 4096);
    mb->block.din->type = type;
    mb->block.din->links = 1;
    mb->block.din->blocks = 0; 
    mb->dirty = 1;
    sfs_put_inode(mb);
    return ino;
}

int sfs_open(const char* path, uint32_t flags) {
    if (!sfs_fs) sfs_init();

    // Parse path
    // Start from root (ino 1)
    struct sfs_memory_block *cwd = sfs_get_inode(1);
    
    char name[SFS_MAX_FILENAME_LEN + 1];
    const char *p = path;
    if (*p == '/') p++; // Skip leading /

    while (*p) {
        // Extract next component
        int i = 0;
        while (*p && *p != '/') {
            if (i < SFS_MAX_FILENAME_LEN) name[i++] = *p;
            p++;
        }
        name[i] = 0;
        if (*p == '/') p++;

        // Lookup name in cwd
        int next_ino = -1;
        struct sfs_inode *din = cwd->block.din;
        
        int n_entries = 4096 / sizeof(struct sfs_entry);
        
        // Simplified lookup loop
        for (int idx = 0; idx < SFS_NDIRECT + 1024; idx++) {
             int bno = sfs_get_block_id(cwd, idx, 0);
             if (bno == 0) break; // End of dir
             
             char *buf = (char *)kmalloc(4096);
             disk_read(bno, (uint8_t *)buf);
             struct sfs_entry *entries = (struct sfs_entry *)buf;
             for (int j = 0; j < n_entries; j++) {
                 if (strlen(entries[j].filename) > 0 && strcmp(entries[j].filename, name) == 0) {
                     next_ino = entries[j].ino;
                     break;
                 }
             }
             kfree(buf);
             if (next_ino != -1) break;
        }

        if (next_ino == -1) {
            // Not found
            if ((flags & SFS_FLAG_WRITE)) {
                // Create
                int type = (*p == 0) ? SFS_FILE : SFS_DIRECTORY; 
                
                int new_ino = sfs_create_inode(type);
                if (new_ino < 0) {
                    sfs_put_inode(cwd);
                    return -1;
                }
                
                // Initialize . and .. for directory
                if (type == SFS_DIRECTORY) {
                    struct sfs_memory_block *new_dir = sfs_get_inode(new_ino);
                    sfs_dir_link(new_dir, ".", new_ino);
                    sfs_dir_link(new_dir, "..", cwd->blockno);
                    sfs_put_inode(new_dir);
                }
                
                sfs_dir_link(cwd, name, new_ino);
                next_ino = new_ino;
            } else {
                sfs_put_inode(cwd);
                return -1;
            }
        }

        struct sfs_memory_block *next = sfs_get_inode(next_ino);
        sfs_put_inode(cwd);
        cwd = next;
    }

    // Found/Created file, cwd is now the file inode
    // Allocate fd
    int fd = -1;
    for (int i = 0; i < 16; i++) {
        if (current->fs.fds[i] == NULL) {
            fd = i;
            break;
        }
    }
    if (fd == -1) {
        sfs_put_inode(cwd);
        return -1;
    }

    struct file *f = (struct file *)kmalloc(sizeof(struct file));
    f->inode = cwd->block.din; // Pointer to din inside mb
    f->path = NULL; // Not used
    f->flags = flags;
    f->off = 0;
    f->mb = cwd; // Store mb to keep ref count and dirty
    
    current->fs.fds[fd] = f;
    return fd;
}

int sfs_close(int fd) {
    if (fd < 0 || fd >= 16 || current->fs.fds[fd] == NULL) return -1;
    struct file *f = current->fs.fds[fd];
    
    sfs_put_inode(f->mb);
    kfree(f);
    current->fs.fds[fd] = NULL;
    return 0;
}

int sfs_seek(int fd, int32_t off, int fromwhere) {
    if (fd < 0 || fd >= 16 || current->fs.fds[fd] == NULL) return -1;
    struct file *f = current->fs.fds[fd];
    
    uint32_t new_off = f->off;
    if (fromwhere == SEEK_SET) {
        new_off = off;
    } else if (fromwhere == SEEK_CUR) {
        new_off += off;
    } else if (fromwhere == SEEK_END) {
        new_off = f->inode->size + off;
    }
    
    f->off = new_off;
    return 0;
}

int sfs_read(int fd, char* buf, uint32_t len) {
    if (fd < 0 || fd >= 16 || current->fs.fds[fd] == NULL) return -1;
    struct file *f = current->fs.fds[fd];
    
    if (f->off >= f->inode->size) return 0;
    if (f->off + len > f->inode->size) len = f->inode->size - f->off;
    
    uint32_t read_len = 0;
    while (read_len < len) {
        uint32_t blk_idx = f->off / 4096;
        uint32_t blk_off = f->off % 4096;
        uint32_t copy_len = 4096 - blk_off;
        if (copy_len > len - read_len) copy_len = len - read_len;
        
        int bno = sfs_get_block_id(f->mb, blk_idx, 0);
        if (bno == 0) {
            break;
        }
        
        char *kbuf = (char *)kmalloc(4096);
        disk_read(bno, (uint8_t *)kbuf);
        memcpy(buf + read_len, kbuf + blk_off, copy_len);
        kfree(kbuf);
        
        f->off += copy_len;
        read_len += copy_len;
    }
    return read_len;
}

int sfs_write(int fd, char* buf, uint32_t len) {
    if (fd < 0 || fd >= 16 || current->fs.fds[fd] == NULL) return -1;
    struct file *f = current->fs.fds[fd];
    
    uint32_t written_len = 0;
    while (written_len < len) {
        uint32_t blk_idx = f->off / 4096;
        uint32_t blk_off = f->off % 4096;
        uint32_t copy_len = 4096 - blk_off;
        if (copy_len > len - written_len) copy_len = len - written_len;
        
        int bno = sfs_get_block_id(f->mb, blk_idx, 1); // Create if needed
        if (bno < 0) return -1;
        
        char *kbuf = (char *)kmalloc(4096);
        disk_read(bno, (uint8_t *)kbuf); // Read first (RMW)
        memcpy(kbuf + blk_off, buf + written_len, copy_len);
        disk_write(bno, (uint8_t *)kbuf);
        kfree(kbuf);
        
        f->off += copy_len;
        written_len += copy_len;
        
        if (f->off > f->inode->size) {
            f->inode->size = f->off;
            f->mb->dirty = 1;
        }
    }
    return written_len;
}

int sfs_get_files(const char* path, char* files[]) {
    if (!sfs_fs) sfs_init();
    
    int fd = sfs_open(path, SFS_FLAG_READ);
    if (fd < 0) return -1;
    
    struct file *f = current->fs.fds[fd];
    if (f->inode->type != SFS_DIRECTORY) {
        sfs_close(fd);
        return 0;
    }
    
    int count = 0;
    
    f->off = 0;
    
    struct sfs_entry entry;
    while (sfs_read(fd, (char *)&entry, sizeof(struct sfs_entry)) == sizeof(struct sfs_entry)) {
        if (strlen(entry.filename) > 0) {
            strcpy(files[count], entry.filename);
            count++;
        }
    }
    
    sfs_close(fd);
    return count;
}