#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "ext2.h"
#include "ext2_helpers.h"

unsigned char* disk;

int valid_directory(struct ext2_dir_entry* dir) {
    if (dir->name_len == 1 && strcmp(dir->name, ".") == 0) {
        return 0;
    }
    if (dir->name_len == 2 && strcmp(dir->name, "..") == 0) {
        return 0;
    }
    if (dir->inode == 0 || dir->inode == 11) {
        return 0;
    }
    return 1;
}

void recurse(int inode_no) {
    int i = 0;
    struct ext2_inode* inode = get_inode_from_no(inode_no);
    for (; i < 12; i++) {
        if (inode->i_block[i] == 0) {
            continue;
        }
        int block = inode->i_block[i];
        struct ext2_dir_entry* cur;
        struct ext2_dir_entry* prev = NULL;
        int pos = 0;
        while (pos < EXT2_BLOCK_SIZE) {
            cur = (struct ext2_dir_entry*) ((unsigned char*) disk + block*EXT2_BLOCK_SIZE + pos);
            if (!cur) {
                break;
            }
            if (cur->rec_len == 0) {
                break;
            }
            pos += cur->rec_len;
            if (valid_directory(cur)) {
                if (cur->file_type == EXT2_FT_DIR) {
                    recurse(cur->inode);
                }
                delete(inode, cur->name, cur->name_len);
                if (prev != NULL) {
                    prev->rec_len += cur->rec_len;
                } else {
                    cur->inode = 0;
                }
            }
            prev = cur;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: ./ext2_rm -r image_path absolute_path\n");
        exit(1);
    }
    if (!absolute_path(argv[2])) {
        return -ENOENT;
    }

    disk = read_disk(argv[1]);
    char* path = get_path(argv[3]);
    char* filename = get_filename(argv[3]);
    int parent_no = check_path_exists(path);
    struct ext2_inode* parent = get_inode_from_no(parent_no);
    struct ext2_dir_entry* file_entry = search_directory(parent, filename);
    if (!file_entry) {
        return -ENOENT;
    }
    struct ext2_dir_entry* prev = get_prev_entry();
    int dir_inode_no = file_entry->inode;
    struct ext2_inode* dir_inode = get_inode_from_no(file_entry->inode);
    //TODO:
    // Add dir functionality
    if (file_entry->file_type == EXT2_FT_DIR) {
        recurse(dir_inode_no);
    }


    prev->rec_len += file_entry->rec_len;
    if (dir_inode->i_links_count == 0) {
        dir_inode->i_dtime = time(NULL);
        reset_all_blocks(dir_inode);
        reset_inode(dir_inode_no - 1);
    }
    return 0;
}