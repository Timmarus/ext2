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

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: ext2_cp <virtual disk name> <source file> <target path>\n");
        exit(1);
    }
    if (!absolute_path(argv[3])) {
        return -ENOENT;
    }
    disk = read_disk(argv[1]);

    FILE* f = fopen(argv[2], "r");
    if (!f) {
        return ENOENT;
    }

    fseek(f, 0, SEEK_END);
    int fsize = ftell(f);
    if (fsize > super_block()->s_free_blocks_count * EXT2_BLOCK_SIZE) {
        fclose(f);
        return ENOSPC;
    }
    fseek(f, 0, SEEK_SET);
    // Splitting filepath into path and filename.
    char* path = get_path(argv[3]);
    char* filename = get_filename(argv[3]);
    char buf[fsize+1];
    buf[fsize] = '\0';
    int parent_no = check_path_exists(path);
    struct ext2_inode* parent = get_inode_from_no(parent_no);
    
    // if (!is_directory(parent)) {
    //     fprintf(stderr, "Error: Parent not directory\n");
    //     return -ENOENT;
    // }
    // Making sure the directory does not already exist
    if (search_directory(parent, filename) != NULL) {
        return -EEXIST;
    }
    int new_no = allocate_inode();
    struct ext2_inode* new = (struct ext2_inode*) (inode_table() + new_no - 1);
    init_inode(new);
    new->i_mode = EXT2_S_IFREG;
    new->i_links_count = 1;
    new->i_blocks = 0;
    new->i_size = 0;

    dir_insert(parent , new_no, filename, EXT2_FT_REG_FILE);
    set_inode(new_no-1);
    int finished = 0;
    while (!finished) {
        fread(buf, fsize, 1, f);
        inode_write(new, buf, strlen(buf));
        if (ftell(f) >= fsize) {
            finished = 1;
        }
    }
    fclose(f);
    return 0;
}
