#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "ext2.h"
#include "ext2_helpers.h"

/* Global variables */

extern unsigned char* disk;
struct ext2_dir_entry* prev_entry = NULL; // For storing the previous entry for ext2_rm.

/* String Manipulation */

char* remove_trailing_slashes(char* str) {
    char* ret = (char*) malloc(sizeof(str));
    strcpy(ret, str);
    while (ret[strlen(ret)-1] == '/') {
        ret[strlen(ret)-1] = '\0';
    }
    return ret;
}

char* get_path(char* orig_path) {
    char* path = remove_trailing_slashes(orig_path);
    char *parent = (char *)malloc (strlen(path));
    strcpy (parent, path);
    char *pos = strrchr (parent, '/');
    *pos = '\0';
    return parent;
}

char* get_filename(char* orig_path) {
    char* path = remove_trailing_slashes(orig_path);
    char *part1 = (char *)malloc (strlen(path));
    strcpy (part1, path);
    char *pos = strrchr (part1, '/');
    *pos = '\0';
    char* filename = strdup (pos + 1);
    return filename;
}

int absolute_path(char* path) { return path[0] == '/'; }


/* Special getters */


struct ext2_super_block* super_block() {
    return (struct ext2_super_block*) (disk + EXT2_BLOCK_SIZE);
}

struct ext2_group_desc* group_desc() {
    return (struct ext2_group_desc*) (disk + EXT2_BLOCK_SIZE * 2);
}

struct ext2_inode* inode_table() {
    return (struct ext2_inode*) ((unsigned char*) disk + EXT2_BLOCK_SIZE * group_desc()->bg_inode_table);
}

unsigned char* block_bitmap() {
    return (unsigned char*) disk + EXT2_BLOCK_SIZE * (group_desc())->bg_block_bitmap;
}

unsigned char* inode_bitmap() {
    return (unsigned char*) disk + EXT2_BLOCK_SIZE * (group_desc())->bg_inode_bitmap;
}

struct ext2_inode* get_inode_from_no(int no) {
    return (struct ext2_inode*) (inode_table() + no - 1);
}

struct ext2_dir_entry* get_prev_entry() {
    return prev_entry;
}

int get_rec_len(int x) {
    if (x % 4 == 0) {
        return 8 + x;
    }
    return 8 + x + (4 - (x%4));
}

/* Memory allocation */


/*
Allocates space within a bitmap, i.e. sets the first 0 bit of the bitmap to 1 and returns the index of that resource within the bitmap.
Returns the number of that resource.
*/
int allocate(unsigned char* bmp, int reserved, int size) {
    int k = 0;
    for (int i = 0; i < sizeof(bmp); i++) {
        unsigned char byte = bmp[i];

        for (int bit = 0; bit < 8; bit++) {
            if (k < 11) {
                k++;
                continue;
            }
            if (!(byte & (1 << bit))) {
                bmp[i] |= 1 << bit;
                return (i*8) + bit + 1;
            }
        }
    }
    exit(ENOSPC); // If we're here, there are no bits open.
}

int allocate_inode() {
    // group_desc()->bg_free_inodes_count--;
    // super_block()->s_free_inodes_count--;
    return allocate(inode_bitmap(), super_block()->s_first_ino, super_block()->s_inodes_count / 8);
}

void init_inode(struct ext2_inode* inode) {
    inode->i_uid = 15;
    inode->i_size = 0;
    inode->i_blocks = 0;
    for (int i = 0 ; i < 15 ; i++) {
        inode->i_block[i] = 0;
    }
    inode->i_gid = 0;
    inode->osd1 = 0;
    inode->i_file_acl = 0;
    inode->i_dir_acl = 0;
    inode->i_faddr = 0;
    inode->i_links_count = 1;
    inode->i_dtime = 0;
    inode->i_ctime = time(NULL);
}

void set_inode(int bit) {
    unsigned char* byte = inode_bitmap() + (bit / 8);
    *byte |= 1 << (bit % 8);
    group_desc()->bg_free_inodes_count--;
    super_block()->s_free_inodes_count--;
}

void reset_inode(int bit) {
    unsigned char* byte = inode_bitmap() + (bit / 8);
    *byte &= ~(1 << (bit % 8));
    group_desc()->bg_free_inodes_count++;
    super_block()->s_free_inodes_count++;
}

int inode_is_set(int bit) {
    unsigned char* byte = inode_bitmap() + (bit / 8);
    return (*byte & (1 << bit % 8));
}

void set_all_blocks(struct ext2_inode* inode) {
    int i = 0;
    for (; i < 12 && inode->i_block[i] != 0; i++) {
        set_block(inode->i_block[i] - 1);
    }
    if (i == 12 && inode->i_block[12] != 0) {
	unsigned int* indirect_block = (unsigned int*) (((char*) disk) + EXT2_BLOCK_SIZE*inode->i_block[12]);
        for (int j = 0; j < EXT2_BLOCK_SIZE / sizeof(unsigned int); j++) {
            if (indirect_block[j] != 0) {
                set_block(indirect_block[j] - 1);
            }
        }
        set_block(inode->i_block[12] - 1);
    }
}

void reset_all_blocks(struct ext2_inode* inode) {
    int i = 0;
    for (; i < 12 && inode->i_block[i] != 0; i++) {
        reset_block(inode->i_block[i] - 1);
    }
    if (i == 12 && inode->i_block[12] != 0) {
        unsigned int* indirect_block = (unsigned int*) (((char*) disk) + EXT2_BLOCK_SIZE*inode->i_block[12]);
        for (int j = 0; j < EXT2_BLOCK_SIZE / sizeof(unsigned int); j++) {
            if (indirect_block[j] != 0) {
                reset_block(indirect_block[j] - 1);
            }
        }
	reset_block(inode->i_block[12] - 1);
    }
}

void set_block(int bit) {
    unsigned char* byte = block_bitmap() + (bit / 8);
    *byte |= 1 << (bit % 8);
    group_desc()->bg_free_blocks_count--;
    super_block()->s_free_blocks_count--;
}

void reset_block(int bit) {
    unsigned char* byte = block_bitmap() + (bit / 8);
    *byte &= ~(1 << (bit % 8));
    group_desc()->bg_free_blocks_count++;
    super_block()->s_free_blocks_count++;
}

int allocate_block() {
    group_desc()->bg_free_blocks_count--;
    super_block()->s_free_blocks_count--;
    return allocate(block_bitmap(), 0, super_block()->s_blocks_count / 8);
}

int block_is_set(int bit) {
    unsigned char* byte = block_bitmap() + (bit / 8);
    return (*byte & (1 << bit % 8));
}

int blocks_overwritten(struct ext2_inode* inode) {
    int i = 0;
    for (; i < 12 && inode->i_block[i] != 0; i++) {
        if (block_is_set(inode->i_block[i] - 1)) {
            return 1;
        }
    }
    if (i == 12) {
        for (int j = 0; j < EXT2_BLOCK_SIZE / sizeof(unsigned int); j++) {
            if (inode->i_block[i] != 0) {
                if (block_is_set(inode->i_block[i] - 1)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

unsigned char* read_disk(char* path) {
    int fd = open(path, O_RDWR);

    disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ 
                | PROT_WRITE, MAP_SHARED, fd, 0);

    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    return disk;
}

/* Directory Manipulation */

int is_directory(struct ext2_inode* inode) {
    return (inode->i_mode & EXT2_S_IFDIR);
}

struct ext2_dir_entry* last_entry(struct ext2_dir_entry *dir) {
    if (dir->rec_len == 0) {
        return 0;
    }
    for (int i = 0; i < EXT2_BLOCK_SIZE; i += dir->rec_len) {
        if (dir->rec_len == 0) {
            break;
        }
        if (get_rec_len(dir->name_len) != dir->name_len) {
            break;
        }
        dir = (struct ext2_dir_entry*) (((unsigned char*) dir) + dir->rec_len);
    }
    return dir;
}

// TODO:
// Support for indirect blocks.
struct ext2_dir_entry* search_directory(struct ext2_inode* inode, char* name) {
    if (!is_directory(inode)) {
        exit(ENOENT);
    }
    int i = 0;
    for (; i < 12; i++) {
        if (inode->i_block[i] == 0) {
            continue;
        }
        int block = inode->i_block[i];
        struct ext2_dir_entry* cur;
        int pos = 0;
        while (pos < EXT2_BLOCK_SIZE) {
            cur = (struct ext2_dir_entry*) ((unsigned char*) disk + block*EXT2_BLOCK_SIZE + pos);
            if (!cur) {
                return NULL;
            }
            if (cur->rec_len == 0) {
                return NULL;
            }
            if (strlen(name) == cur->name_len && strncmp(cur->name, name, cur->name_len) == 0) {
                return cur;
            }
            pos += cur->rec_len;
            prev_entry = cur;
        }
    }
    return NULL;
}

int check_hiding(struct ext2_dir_entry* cur, char* name) {
    int left = cur->rec_len;
    int needed = get_rec_len(strlen(name));
    int pos = 0;
    while (left > 0 && left >= needed && cur->rec_len != 0 && cur->name_len != 0) {
        if (cur->name_len == strlen(name) && strncmp(cur->name, name, strlen(name)) == 0) {
            return pos;
        }
        left -= get_rec_len(cur->name_len);
        pos += get_rec_len(cur->name_len);
        cur = (struct ext2_dir_entry*) (((unsigned char*) cur) + get_rec_len(cur->name_len));
    }
    return 0;
}


struct ext2_dir_entry* search_and_restore(struct ext2_inode* parent, char* name) {
    int i = 0;
    for (; i < 12; i++) {
        if (parent->i_block[i] == 0) {
            continue;
        }
        int block = parent->i_block[i];
        struct ext2_dir_entry* cur;
        int pos = 0;
        while (pos < EXT2_BLOCK_SIZE) {
            cur = (struct ext2_dir_entry*) ((unsigned char*) disk + block * EXT2_BLOCK_SIZE + pos);
            if (!cur) {
                return NULL;
            }
            if (cur->rec_len == 0) {
                return NULL;
            }
            int offset = check_hiding(cur, name);
            struct ext2_dir_entry* hidden = (struct ext2_dir_entry*) (((unsigned char*) cur) + offset);
            if (offset != 0) {
                if (inode_is_set(hidden->inode - 1)) {
                    exit(ENOENT);
                }
                if (blocks_overwritten(get_inode_from_no(hidden->inode))) {
                    exit(ENOENT);
                }
                int padding = cur->rec_len - offset;
                cur->rec_len = offset;
                if (padding > get_rec_len(hidden->name_len)) {
                    hidden->rec_len = padding;
                } else {
                    hidden->rec_len = get_rec_len(hidden->name_len);
                }
                return hidden;
            }
            pos += cur->rec_len;
        }
    }
    return NULL;
}

struct ext2_dir_entry* find_space(int block, char* filename, int fnamelen) {
    struct ext2_dir_entry* dir;// = (struct ext2_dir_entry*) (((char*) disk) + block*EXT2_BLOCK_SIZE);
    int i = 0;
    while (i < EXT2_BLOCK_SIZE) {
        dir = (struct ext2_dir_entry*) (((char*) disk) + block*EXT2_BLOCK_SIZE + i);
        if (dir->rec_len == 0 ||dir->inode == 0 || dir->inode > super_block()->s_inodes_per_group) {
            dir->rec_len = EXT2_BLOCK_SIZE - i;
            return dir;
        }
        if (dir->rec_len >= EXT2_BLOCK_SIZE - i) {
            int len = get_rec_len(dir->name_len);
            dir->rec_len = len;
            dir = (struct ext2_dir_entry*) (((unsigned char*) dir) + len);
            dir->rec_len = EXT2_BLOCK_SIZE - (i + len);
            return dir;
        }
	    i += dir->rec_len;
        //dir = (struct ext2_dir_entry*) (((unsigned char*) dir) + dir->rec_len);
    }
    return NULL;
}

//TODO:
// Support for indirect blocks (maybe?)
struct ext2_dir_entry* dir_insert(struct ext2_inode* parent, int inode, char* name, unsigned int type) {
    int name_length = strlen(name);
    struct ext2_dir_entry* new = NULL;
    int i = 0;
    for (; new == NULL && i < 12; i++) {
        new = find_space(parent->i_block[i], name, name_length);
        i++;
    }
    // if (new->rec_len < EXT2_BLOCK_SIZE) {
    //     new->rec_len = get_rec_len(new->name_len);
    //     new = (struct ext2_dir_entry*) (((unsigned char *) new) + get_rec_len(new->name_len));
    // }
    // else {
    //     new = (struct ext2_dir_entry*) (((unsigned char *) new) + get_rec_len(new->name_len));
    // }
    new->inode = inode;
    if (new->rec_len == 0) {
        new->rec_len = get_rec_len(name_length);
    }
    new->name_len = name_length;
    new->file_type = type;
    strncpy(new->name, name, name_length);
    return new;
}

/*
Checks that the given path exists, and returns the inode of the file or directory's parent.
*/
int check_path_exists(char* path) {
    unsigned int cur_no = EXT2_ROOT_INO;
    struct ext2_inode* cur = get_inode_from_no(cur_no);
    if (cur < 0) {
        exit(ENOENT);
    }
    char* tok;
    if (strcmp(path, "") != 0) {
        tok = strtok(path, "/");
    }
    else {
        tok = NULL;
    }
    struct ext2_dir_entry* ret;
    while (tok != NULL) {
        if (!is_directory(cur)) {
            exit(ENOENT);
        }
        ret = search_directory(cur, tok);
        if (!ret) {
            exit(ENOENT);
        }
        cur_no = ret->inode;
        cur = get_inode_from_no(cur_no);
        tok = strtok(NULL, "/");
    }
    return cur_no;
}

int create_indirect_block(struct ext2_inode* inode, int block) {
    if (inode->i_block[12] == 0) {
        inode->i_block[12] = block;
        inode->i_blocks += 2;
        //set_block(block - 1);
	return 1;
    }
    return 0;
}

int  add_block_to_inode(struct ext2_inode* inode, int block_no) {
    int i = 0;
    for (; i < 12; i++) {
        if (inode->i_block[i] == 0) {
            inode->i_block[i] = block_no;
            inode->i_blocks += 2;
            return block_no;
        }
    }
    int created = create_indirect_block(inode, block_no);
    int new_block_no;
    if (created) {
	new_block_no = allocate_block();
        //set_block(new_block_no - 1);
    } else {
	new_block_no = block_no;
    }
    unsigned int* block = (unsigned int*) (((unsigned char*) disk) + EXT2_BLOCK_SIZE*inode->i_block[12]);
    for (i = 0; i < EXT2_BLOCK_SIZE / sizeof(unsigned int); i++) {
        if (block[i] == 0) {
            block[i] = new_block_no;
            inode->i_blocks += 2;
            return new_block_no;
        }
    } 
    exit(ENOSPC);
}

int inode_write(struct ext2_inode* inode, char* data, int len) {
    int remaining = len;
    while (remaining != 0) {
        int block_no = allocate_block();
        //unsigned char* block = (((unsigned char*) disk) + EXT2_BLOCK_SIZE*(block_no));
        if (remaining > EXT2_BLOCK_SIZE) {
            inode->i_size += EXT2_BLOCK_SIZE;
            int new_block_no = add_block_to_inode(inode, block_no);
	    memcpy((((unsigned char*) disk) + EXT2_BLOCK_SIZE*(new_block_no)), data, EXT2_BLOCK_SIZE);
            data = data + EXT2_BLOCK_SIZE;
            remaining -= EXT2_BLOCK_SIZE;
        }
        else {
            inode->i_size += remaining;
	    int new_block_no = add_block_to_inode(inode, block_no);
            memcpy((((unsigned char*) disk) + EXT2_BLOCK_SIZE*(new_block_no)), data, remaining);
            return 0;
        }
    }
    return 0;
}

int get_free_bits_count(unsigned char* bmp, int size) {
    int i = 0;
    for (int byte = 0; byte < size; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            if (!(bmp[byte] & (1 << bit))) {
                i++;
            }
        }
    }
    return i;
}

void delete(struct ext2_inode* parent, char* name, int name_length) {
    char search_name[name_length+1];
    search_name[name_length] = '\0';
    strncpy(search_name, name, name_length);
    struct ext2_dir_entry* file_entry = search_directory(parent, search_name);
    int inode_no = file_entry->inode;
    struct ext2_inode* inode = get_inode_from_no(file_entry->inode);
    
    struct ext2_dir_entry* prev = get_prev_entry();
    
    inode->i_links_count--;
    if (prev != NULL) {
        prev->rec_len += file_entry->rec_len;
    }
    if (inode->i_links_count == 0 || inode->i_mode & EXT2_S_IFDIR) {
        inode->i_dtime = time(NULL);
        reset_all_blocks(inode);
        reset_inode(inode_no - 1);
    }
    return;
}