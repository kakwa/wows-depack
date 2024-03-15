#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "wows-depack.h"
#include "wows-depack-private.h"
#include "hashmap.h"

int print_header(WOWS_INDEX_HEADER *header) {
    printf("* magic:                     %.4s\n", (char *)&header->magic);
    printf("* endianess:                 0x%x\n", header->endianess);
    printf("* id:                        0x%x\n", header->id);
    printf("* unknown_2:                 0x%x\n", header->unknown_2);
    printf("* file_dir_count:            %u\n", header->file_dir_count);
    printf("* file_count:                %u\n", header->file_count);
    printf("* unknown_3:                 %lu\n", header->unknown_3);
    printf("* header_size:               %lu\n", header->header_size);
    printf("* offset_idx_data_section:   0x%lx\n", header->offset_idx_data_section);
    printf("* offset_idx_footer_section: 0x%lx\n", header->offset_idx_footer_section);
    return 0;
}

int print_footer(WOWS_INDEX_FOOTER *footer) {
    printf("* pkg_file_name_size:        %lu\n", footer->pkg_file_name_size);
    printf("* unknown_7:                 0x%lx\n", footer->unknown_7);
    printf("* id:                        0x%lx\n", footer->id);
    return 0;
}

int print_metadata_entry(WOWS_INDEX_METADATA_ENTRY *entry) {
    printf("* file_name_size:            %lu\n", entry->file_name_size);
    printf("* offset_idx_file_name:      0x%lx\n", entry->offset_idx_file_name);
    printf("* id:                        0x%lx\n", entry->id);
    printf("* parent_id:                 0x%lx\n", entry->parent_id);
    return 0;
}

int print_data_file_entry(WOWS_INDEX_DATA_FILE_ENTRY *entry) {
    printf("* metadata_id:               0x%lx\n", entry->metadata_id);
    printf("* footer_id:                 0x%lx\n", entry->footer_id);
    printf("* offset_pkg_data:           0x%lx\n", entry->offset_pkg_data);
    printf("* type_1:                    0x%x\n", entry->type_1);
    printf("* type_2:                    0x%x\n", entry->type_2);
    printf("* size_pkg_data:             0x%x\n", entry->size_pkg_data);
    printf("* id_pkg_data:               0x%lx\n", entry->id_pkg_data);
    printf("* padding:                   0x%x\n", entry->padding);
    return 0;
}

int print_debug_raw(WOWS_INDEX *index) {
    WOWS_INDEX_HEADER *header = index->header;
    WOWS_INDEX_METADATA_ENTRY *metadatas = index->metadata;
    WOWS_INDEX_DATA_FILE_ENTRY *data_file_entry = index->data_file_entry;
    WOWS_INDEX_FOOTER *footer = index->footer;
    int i;
    // Print the header
    printf("Index Header Content:\n");
    print_header(header);
    printf("\n");

    // Print the metadata entries
    for (i = 0; i < header->file_dir_count; i++) {
        WOWS_INDEX_METADATA_ENTRY *entry = &metadatas[i];
        printf("Metadata entry [%d]:\n", i);
        print_metadata_entry(entry);
        char *filename = entry->_file_name;
        printf("* filename:                  %.*s\n", (int)entry->file_name_size, filename);
        printf("\n");
    }

    // Print the pkg file entries
    for (i = 0; i < header->file_count; i++) {
        WOWS_INDEX_DATA_FILE_ENTRY *entry = &data_file_entry[i];
        printf("Data file entry [%d]:\n", i);
        print_data_file_entry(entry);
        printf("\n");
    }

    // Print the footer

    printf("Index Footer Content:\n");
    print_footer(footer);
    char *pkg_filename = footer->_file_name;
    printf("* pkg filename:              %.*s\n", (int)footer->pkg_file_name_size, pkg_filename);
    printf("\n");
    return 0;
}

int print_debug_files(WOWS_INDEX *index, struct hashmap *map) {
    WOWS_INDEX_HEADER *header = index->header;
    WOWS_INDEX_DATA_FILE_ENTRY *data_file_entry = index->data_file_entry;
    int i;
    // Print the actual files
    for (i = 0; i < header->file_count; i++) {
        WOWS_INDEX_DATA_FILE_ENTRY *fentry = &data_file_entry[i];
        WOWS_INDEX_METADATA_ENTRY *mentry_search = &(WOWS_INDEX_METADATA_ENTRY){.id = fentry->metadata_id};
        void *res = hashmap_get(map, &mentry_search);
        if (res == NULL) {
            return WOWS_ERROR_MISSING_METADATA_ENTRY;
        }
        WOWS_INDEX_METADATA_ENTRY *mentry = *(WOWS_INDEX_METADATA_ENTRY **)res;

        printf("File entry [%d]:\n", i);
        print_data_file_entry(fentry);
        print_metadata_entry(mentry);
        char *filename = mentry->_file_name;
        printf("* filename:                  %.*s\n", (int)mentry->file_name_size, filename);
        WOWS_INDEX_METADATA_ENTRY *m_parent_entry_search = &(WOWS_INDEX_METADATA_ENTRY){.id = mentry->parent_id};
        WOWS_INDEX_METADATA_ENTRY **mparent_entry =
            (WOWS_INDEX_METADATA_ENTRY **)hashmap_get(map, &m_parent_entry_search);
        int level = 1;
        while (mparent_entry != NULL && level < WOWS_DIR_MAX_LEVEL) {
            printf("parent [%d]:\n", level);
            print_metadata_entry(*mparent_entry);
            char *filename = (*mparent_entry)->_file_name;
            printf("* filename:                  %.*s\n", (int)mentry->file_name_size, filename);
            mentry_search = &(WOWS_INDEX_METADATA_ENTRY){.id = (*mparent_entry)->parent_id};
            mparent_entry = NULL;
            mparent_entry = (WOWS_INDEX_METADATA_ENTRY **)hashmap_get(map, &mentry_search);
            level++;
        }
        printf("\n");
    }
    return 0;
}

// FIXME, we should take inspiration from tree, not display pipes '|' at every indent levels
// For inspiration: https://github.com/mojadita/tree/blob/7db7c7f01133fa78bb5bfdda2713373a64582d9d/process.c
bool dir_inode_print_tree(const void *item, void *udata) {
    int level = *((int *)udata);
    level++;
    print_inode_tree(*(WOWS_BASE_INODE **)item, level);
    return true;
}

int print_inode_tree(WOWS_BASE_INODE *inode, int level) {
    for (int i = 1; i < level; i++) {
        printf(" | ");
    }
    if (level != 0) {
        printf(" |-");
    }

    if (inode->type == WOWS_INODE_TYPE_FILE) {
        WOWS_FILE_INODE *file_inode = (WOWS_FILE_INODE *)inode;
        printf("* %s\n", file_inode->name);
    }
    if (inode->type == WOWS_INODE_TYPE_DIR) {
        printf("-%s/\n", inode->name);
        WOWS_DIR_INODE *dir_inode = (WOWS_DIR_INODE *)inode;
        struct hashmap *map = dir_inode->children_inodes;
        hashmap_scan(map, dir_inode_print_tree, &level);
    }
    return 0;
}

int wows_print_tree(WOWS_CONTEXT *context) {
    print_inode_tree(context->root, 0);
    return 0;
}

bool dir_inode_print_flat(const void *item, void *udata) {
    print_inode_flat(*(WOWS_BASE_INODE **)item);
    return true;
}

int print_inode_flat(WOWS_BASE_INODE *inode) {
    if (inode->type == WOWS_INODE_TYPE_FILE) {
        WOWS_FILE_INODE *file_inode = (WOWS_FILE_INODE *)inode;
        int depth;
        char **parent_entries = calloc(WOWS_DIR_MAX_LEVEL, sizeof(char *));
        get_path_inode(inode, &depth, parent_entries);

        printf("/");
        for (int j = (depth - 1); j > -1; j--) {
            printf("%s/", parent_entries[j]);
        }
        printf("%s\n", file_inode->name);
        free(parent_entries);
    }
    if (inode->type == WOWS_INODE_TYPE_DIR) {
        WOWS_DIR_INODE *dir_inode = (WOWS_DIR_INODE *)inode;
        struct hashmap *map = dir_inode->children_inodes;
        hashmap_scan(map, dir_inode_print_flat, NULL);
    }
    return 0;
}

int wows_print_flat(WOWS_CONTEXT *context) {
    print_inode_flat(context->root);
    return 0;
}
