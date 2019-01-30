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

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: ./ext2_rm image_path absolute_path\n");
        exit(1);
    }
    if (!absolute_path(argv[2])) {
        return -ENOENT;
    }

    disk = read_disk(argv[1]);
    char* path = get_path(argv[2]);
    char* filename = get_filename(argv[2]);
    int parent_no = check_path_exists(path);
    struct ext2_inode* parent = get_inode_from_no(parent_no);
    struct ext2_dir_entry* file_entry = search_directory(parent, filename);
    if (!file_entry) {
        return -ENOENT;
    }
    //TODO:
    // Add dir functionality
    if (file_entry->file_type == EXT2_FT_DIR) {
        return -EISDIR;
    }

    int inode_no = file_entry->inode;
    struct ext2_inode* inode = get_inode_from_no(file_entry->inode);
    struct ext2_dir_entry* prev = get_prev_entry();
    
    inode->i_links_count--;
    if (prev != NULL) {
        prev->rec_len += file_entry->rec_len;
    }
    if (inode->i_links_count == 0) {
        inode->i_dtime = time(NULL);
        reset_all_blocks(inode);
        reset_inode(inode_no - 1);
    }
    return 0;
}