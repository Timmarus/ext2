#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "ext2.h"
#include "ext2_helpers.h"

unsigned char* disk;

//TODO:
// Support for trailing slashes.
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: ./ext2_mkdir imagepath newdir\n");
        exit(1);
    }
    if (!absolute_path(argv[2])) {
        return ENOENT;
    }
    if (strcmp("/", argv[2]) == 0) {
        return EEXIST;
    }

    disk = read_disk(argv[1]);

    // Splitting filepath into path and filename.
    char* path = get_path(argv[2]);
    char* filename = get_filename(argv[2]);
    int parent_no = check_path_exists(path);
    struct ext2_inode* parent = get_inode_from_no(parent_no);
    
    // Making sure the directory does not already exist
    if (search_directory(parent, filename) != NULL) {
        return -EEXIST;
    }

    // Allocating a new inode for the new directory.
    int new_no = allocate_inode();
    struct ext2_inode* new = (struct ext2_inode*) (inode_table() + new_no - 1);
    init_inode(new);
    
    // Allocating the new directory's block
    int new_block = allocate_block();
    new->i_block[0] = new_block;
    struct ext2_dir_entry* block = (struct ext2_dir_entry*) (disk + new_block*EXT2_BLOCK_SIZE);
    block->rec_len = 0;

    // Inserting ., .., and the file directory entries in their respective places.
    dir_insert(new, new_no, ".", EXT2_FT_DIR);
    dir_insert(new, parent_no, "..", EXT2_FT_DIR);
    dir_insert(parent , new_no, filename, EXT2_FT_DIR);
    parent->i_links_count++;

    // Initializing the last few.
    new->i_mode = EXT2_S_IFDIR;
    new->i_links_count = 2;
    new->i_blocks = 2;
    new->i_size = EXT2_BLOCK_SIZE;

    // Updating bitmaps.
    //set_block(new_block - 1);
    set_inode(new_no-1);
    group_desc()->bg_used_dirs_count++;
    return 0;
}
