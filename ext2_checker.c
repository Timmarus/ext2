#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include "ext2.h"
#include "ext2_helpers.h"

unsigned char* disk;
int fix_count;

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
    struct ext2_inode* inode = get_inode_from_no(inode_no);
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
                return;
            }
            if (cur->rec_len == 0) {
                return;
            }
            struct ext2_inode* cur_inode = get_inode_from_no(cur->inode);
            int mode = cur_inode->i_mode;
            if (mode & EXT2_S_IFDIR) {
                if (cur->file_type != EXT2_FT_DIR) {
                    cur->file_type = EXT2_FT_DIR;
                    fix_count++;
                    printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", cur->inode);
                }
                if (valid_directory(cur)) {
                    recurse(cur->inode);
                }
            }
            else if (mode & EXT2_S_IFREG) {
                if (cur->file_type != EXT2_FT_REG_FILE) {
                    cur->file_type = EXT2_FT_REG_FILE;
                    fix_count++;
                    printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", cur->inode);
                }
            }
           else  if (mode & EXT2_S_IFLNK) { 
                if (cur->file_type != EXT2_FT_SYMLINK) {
                    cur->file_type = EXT2_FT_SYMLINK;
                    fix_count++;
                    printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", cur->inode);
                }
            }
            if (!inode_is_set(cur->inode - 1)) {
                set_inode(cur->inode - 1);
                fix_count++;
                printf("Fixed: inode [%d] not marked as in-use\n", cur->inode);
            }
            if (cur_inode->i_dtime != 0) {
                cur_inode->i_dtime = 0;
                fix_count++;
                printf("Fixed: valid inode marked for deletion: [%d]\n", cur->inode);
            }
            int sub_count = 0;
            int j = 0;
            for (; j < 12 && cur_inode->i_block[j] != 0; j++) {
                if (cur_inode->i_block[j] != 0) {
                    if (!block_is_set(cur_inode->i_block[j] - 1)) {
                        set_block(cur_inode->i_block[j] - 1);
                        sub_count++;
                    }
                }
            }
            if (i == 12 && inode->i_block[12] != 0) {
                if (!block_is_set(inode->i_block[12])) {
                    set_block(inode->i_block[12] - 1);
                    sub_count++;
                }
                unsigned int* indirect_block = (unsigned int*) (((char*) disk) + EXT2_BLOCK_SIZE*inode->i_block[12]);
                for (int j = 0; j < EXT2_BLOCK_SIZE / sizeof(unsigned int); j++) {
                    if (!block_is_set(indirect_block[j] - 1)) {
                        set_block(indirect_block[j] - 1);
                        sub_count++;
                    }
                }
            }
            fix_count += sub_count;
            if (sub_count) {
                printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", sub_count, cur->inode);
            }

            pos += cur->rec_len;
        }
    }

}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: ./ext2_checker image_path\n");
        exit(1);
    }
    disk = read_disk(argv[1]);
    fix_count = 0;
    int inode_count = get_free_bits_count(inode_bitmap(), super_block()->s_inodes_count / 8);
    int block_count = get_free_bits_count(block_bitmap(), super_block()->s_blocks_count / 8);
    if (super_block()->s_free_inodes_count != inode_count) {
        printf("Fixed: superblock's free inodes counter was off by %d compared to the bitmap\n", abs(super_block()->s_free_inodes_count - inode_count));
        fix_count += abs(super_block()->s_free_inodes_count - inode_count);
        super_block()->s_free_inodes_count = inode_count;
    }
    if (super_block()->s_free_blocks_count != block_count) {
        printf("Fixed: superblock's free blocks counter was off by %d compared to the bitmap\n", abs(super_block()->s_free_blocks_count - block_count));
        fix_count += abs(super_block()->s_free_blocks_count - block_count);
        super_block()->s_free_blocks_count = block_count;
    }
    if (group_desc()->bg_free_inodes_count != inode_count) {
        printf("Fixed: block group's free inodes counter was off by %d compared to the bitmap\n", abs(group_desc()->bg_free_inodes_count - inode_count));
        fix_count += abs(group_desc()->bg_free_inodes_count - inode_count);
        group_desc()->bg_free_inodes_count = inode_count;
    }
    if (group_desc()->bg_free_blocks_count != block_count) {
        printf("Fixed: block group's free blocks counter was off by %d compared to the bitmap\n", abs(group_desc()->bg_free_blocks_count - block_count));
        fix_count += abs(group_desc()->bg_free_blocks_count - block_count);
        group_desc()->bg_free_blocks_count = block_count;
    }
    recurse(EXT2_ROOT_INO);
    if (fix_count) {
        printf("%d file system inconsistencies repaired!\n", fix_count);
    } else {
        printf("No file system inconsistencies repaired!\n");
    }
}
