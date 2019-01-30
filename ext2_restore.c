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
        fprintf(stderr, "Usage: ./ext2_restore image_path absolute_path\n");
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
    if (!is_directory(parent)) {
        return -ENOENT;
    }

    struct ext2_dir_entry* hidden_entry = search_and_restore(parent, filename);
    if (!hidden_entry) {
        return -ENOENT;
    }
    struct ext2_inode* inode = get_inode_from_no(hidden_entry->inode);
    inode->i_dtime = 0;
    inode->i_links_count++;
    set_all_blocks(inode);
    set_inode(hidden_entry->inode - 1);
    return 0;
}