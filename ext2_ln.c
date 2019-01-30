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

//TODO
// Symlinks
int main(int argc, char *argv[]) {
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "Usage: ./ext2_ln image_path [-s] /path/to/link /path/to/source\n");
        exit(1);
    }
    int symlink = argc - 4; // This is 0 for hard links, and 1 for symlinks.
    char* source_arg;
    char* link_arg;
    if (!symlink) {
        source_arg = argv[2];
        link_arg = argv[3];
    } else {
        source_arg = argv[3];
        link_arg = argv[4];
    }
    if (!absolute_path(source_arg) || !absolute_path(link_arg)) {
	return -ENOENT;
    }
    disk = read_disk(argv[1]);
    char full_path[strlen(source_arg)];
    strcpy(full_path, source_arg);
    char* source_path = get_path(source_arg);
    char* source_filename = get_filename(source_arg);
    int source_parent_no = check_path_exists(source_path);
    struct ext2_inode* source_parent = get_inode_from_no(source_parent_no);
    struct ext2_dir_entry* source_entry = search_directory(source_parent, source_filename);
    if (!source_entry) {
        return -ENOENT;
    }
    int source_no = source_entry->inode;
    struct ext2_inode* source_inode = get_inode_from_no(source_no);
    if (is_directory(source_inode)) {
        return -EISDIR;
    }

    char* link_path = get_path(link_arg);
    char* link_filename = get_filename(link_arg);
    int link_parent_no = check_path_exists(link_path);
    struct ext2_inode* link_parent = get_inode_from_no(link_parent_no);
    if (search_directory(link_parent, link_filename) != NULL) {
        return -EEXIST;
    }
    if (!symlink) {
        dir_insert(link_parent, source_no, link_filename, EXT2_FT_REG_FILE);
        source_inode->i_links_count++;
        return 0;
    } else {
        check_path_exists(source_arg);
        int inode_no = allocate_inode();
        struct ext2_inode* inode = get_inode_from_no(inode_no);
        init_inode(inode);
        inode->i_mode = EXT2_S_IFLNK;
        inode_write(inode, full_path, strlen(full_path));
        dir_insert(link_parent, inode_no, link_filename, EXT2_FT_SYMLINK);
	    set_inode(inode_no - 1);
    }
    // TODO:
    // Symlinks.
}
