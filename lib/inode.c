#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "wows-depack.h"
#include "wows-depack-private.h"
#include "hashmap.h"

// Construct the inode tree
int build_inode_tree(WOWS_INDEX *index, int current_index_context, WOWS_CONTEXT *context) {
    // Iterate on all individual files (aka the leaf on our dir/file tree).
    // To do so, we iterate on all the entries of the file section
    for (int i = 0; i < index->header->file_count; i++) {
        WOWS_INDEX_DATA_FILE_ENTRY *fentry = &index->data_file_entry[i];

        // Recover the metadata entry corresponding to our file in the metadata map
        WOWS_INDEX_METADATA_ENTRY *mentry_search = &(WOWS_INDEX_METADATA_ENTRY){.id = fentry->metadata_id};
        void *res = hashmap_get(context->metadata_map, &mentry_search);
        if (res == NULL) {
            return WOWS_ERROR_MISSING_METADATA_ENTRY;
        }
        WOWS_INDEX_METADATA_ENTRY *mentry = *(WOWS_INDEX_METADATA_ENTRY **)res;

        // Get the path branch (all the parent directories up to the root)
        int depth;
        WOWS_INDEX_METADATA_ENTRY **parent_entries;
        parent_entries = calloc(WOWS_DIR_MAX_LEVEL, sizeof(WOWS_INDEX_METADATA_ENTRY *));
        get_path(context, mentry, &depth, parent_entries);

        // Insert the parent directories if necessary
        WOWS_DIR_INODE *parent_inode = context->root;
        // We go in reverse to start from the root to the immediate parent directory of the file
        for (int j = (depth - 1); j > -1; j--) {
            // We try to get the directory from the current dir (parent_inode)
            char *name = parent_entries[j]->_file_name;
            WOWS_DIR_INODE *existing_inode = (WOWS_DIR_INODE *)get_child(parent_inode, name);

            // If the directory inode doesn't exist, we need to create it
            if (existing_inode == NULL) {
                uint64_t metadata_id = parent_entries[j]->id;
                parent_inode = init_dir_inode(metadata_id, current_index_context, parent_inode, context);
                if (parent_inode == NULL) {
                    // For some unknown reason, we failed to create the new inode
                    return WOWS_ERROR_UNKNOWN;
                }
            }
            // Otherwise we just go through it
            else {
                if (existing_inode->type != WOWS_INODE_TYPE_DIR) {
                    // This is a strange error case, we got the wrong inode type
                    // This means there are some id collision between file and dir
                    return WOWS_ERROR_ID_COLLISION_FILE_DIR;
                }
                parent_inode = existing_inode;
            }
        }
        // Finaly, create the file inode to the directory it belongs to.
        uint64_t metadata_id = mentry->id;
        WOWS_FILE_INODE *file_inode = init_file_inode(metadata_id, current_index_context, parent_inode, context);
        if (file_inode == NULL) {
            return WOWS_ERROR_UNKNOWN;
        }
        free(parent_entries);
    }
    return 0;
}

// Add an inode (file or dir) to a given inode (parent dir)
int add_child_inode(WOWS_DIR_INODE *parent_inode, WOWS_BASE_INODE *inode) {
    hashmap_set(parent_inode->children_inodes, &inode);
    return 0;
}

// Recover a child inode by name
// Return NULL if the inode doesn't exist
WOWS_BASE_INODE *get_child(WOWS_DIR_INODE *inode, char *name) {
    WOWS_BASE_INODE *ientry_search = &(WOWS_BASE_INODE){.name = name};
    void *res = hashmap_get(inode->children_inodes, &ientry_search);
    if (res == NULL) {
        return NULL;
    }
    return *(WOWS_BASE_INODE **)res;
}

// Init the root inode (to be used only we initializing the parser context
WOWS_DIR_INODE *init_root_inode() {
    WOWS_DIR_INODE *dir_inode = calloc(sizeof(WOWS_DIR_INODE), 1);
    dir_inode->type = WOWS_INODE_TYPE_DIR;
    dir_inode->id = WOWS_ROOT_INODE;
    dir_inode->index_file_index = WOWS_ROOT_INODE;
    dir_inode->parent_inode = NULL;
    dir_inode->name = ".";
    dir_inode->children_inodes =
        hashmap_new(sizeof(WOWS_BASE_INODE *), 0, 0, 0, dir_inode_hash, dir_inode_compare, NULL, NULL);
    return dir_inode;
}

// create a dir inode
WOWS_DIR_INODE *init_dir_inode(uint64_t metadata_id, uint32_t current_index_context, WOWS_DIR_INODE *parent_inode,
                               WOWS_CONTEXT *context) {
    WOWS_INDEX_METADATA_ENTRY *mentry_search = &(WOWS_INDEX_METADATA_ENTRY){.id = metadata_id};
    void *res = hashmap_get(context->metadata_map, &mentry_search);
    if (res == NULL) {
        return NULL;
    }
    WOWS_INDEX_METADATA_ENTRY *entry = *(WOWS_INDEX_METADATA_ENTRY **)res;
    char *name = entry->_file_name;

    WOWS_DIR_INODE *dir_inode = calloc(sizeof(WOWS_DIR_INODE), 1);
    dir_inode->type = WOWS_INODE_TYPE_DIR;
    dir_inode->id = metadata_id;
    dir_inode->index_file_index = current_index_context;
    dir_inode->parent_inode = parent_inode;
    dir_inode->name = name;
    dir_inode->children_inodes =
        hashmap_new(sizeof(WOWS_BASE_INODE *), 0, 0, 0, dir_inode_hash, dir_inode_compare, NULL, NULL);

    if (parent_inode != NULL) {
        add_child_inode(parent_inode, (WOWS_BASE_INODE *)dir_inode);
    } else {
        free(dir_inode);
        return NULL;
    }
    return dir_inode;
}

// create a file inode
WOWS_FILE_INODE *init_file_inode(uint64_t metadata_id, uint32_t current_index_context, WOWS_DIR_INODE *parent_inode,
                                 WOWS_CONTEXT *context) {
    WOWS_INDEX_METADATA_ENTRY *mentry_search = &(WOWS_INDEX_METADATA_ENTRY){.id = metadata_id};
    void *res = hashmap_get(context->metadata_map, &mentry_search);
    if (res == NULL) {
        return NULL;
    }
    WOWS_INDEX_METADATA_ENTRY *entry = *(WOWS_INDEX_METADATA_ENTRY **)res;
    char *name = entry->_file_name;
    WOWS_FILE_INODE *file_inode = calloc(sizeof(WOWS_FILE_INODE), 1);
    file_inode->type = WOWS_INODE_TYPE_FILE;
    file_inode->id = metadata_id;
    file_inode->name = name;
    file_inode->index_file_index = current_index_context;
    file_inode->parent_inode = parent_inode;
    if (parent_inode != NULL) {
        add_child_inode(parent_inode, (WOWS_BASE_INODE *)file_inode);
    } else {
        free(file_inode);
        return NULL;
    }
    return file_inode;
}

// Get the dir/subdir path of a given file inode
int get_path_inode(WOWS_BASE_INODE *inode, int *depth, char *entries[]) {
    int level = 0;
    *depth = level;

    WOWS_DIR_INODE *dir_inode = inode->parent_inode;
    while (dir_inode->id != WOWS_ROOT_INODE && level < WOWS_DIR_MAX_LEVEL) {
        entries[level] = dir_inode->name;
        dir_inode = dir_inode->parent_inode;
        level++;
    }
    *depth = level;
    return 0;
}
