#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include "wows-depack.h"
#include "wows-depack-private.h"
#include "hashmap.h"

#define FREE_WITH_CLOSE 1
#define FREE_SIMPLE 0

// Check that start and end of section is withing the boundary of the mmaped file
bool checkOutOfIndex(char *start, char *end, WOWS_INDEX *index) {
    if ((index->start_address > start) || (index->end_address < start)) {
        return true;
    }
    if ((index->start_address > end) || (index->end_address < end)) {
        return true;
    }
    return false;
}

// Base context release, it takes a flag to conditionally closing and unmapping files
// (we need to disable that part when dealing with parsing from buffers)
int wows_free_context_internal(WOWS_CONTEXT *context, int flag) {
    for (int i = 0; i < context->index_count; i++) {
        WOWS_INDEX *index = context->indexes[i];
        wows_free_index(index, flag);
    }

    free_inode_tree(context->root);
    free(context->indexes);
    hashmap_free(context->metadata_map);
    hashmap_free(context->file_map);
    if (context->err_msg != NULL) {
        free(context->err_msg);
    }
    free(context);
    return 0;
}

int wows_free_index(WOWS_INDEX *index, int flag) {
    if (flag == FREE_WITH_CLOSE) {
        munmap(index->start_address, index->length);
        close(index->fd);
    }
    free(index->index_file_path);
    for (int j = 0; j < index->header->file_dir_count; j++) {
        free(index->metadata[j]._file_name);
    }
    free(index->metadata);
    free(index->data_file_entry);
    free(index->footer->_file_name);
    free(index->footer);
    free(index->header);
    free(index);
    return 0;
}

// Free the whole context, including un-mmaping the index files;
int wows_free_context(WOWS_CONTEXT *context) {
    return wows_free_context_internal(context, FREE_WITH_CLOSE);
    return 0;
}

// Free without the un-mmapping (in case the index is loaded in another fashion)
int wows_free_context_no_munmap(WOWS_CONTEXT *context) {
    return wows_free_context_internal(context, FREE_SIMPLE);
}

// Helper functions to free the inode tree
bool iter_inode_free(const void *item, void *udata) {
    free_inode_tree(*(WOWS_BASE_INODE **)item);
    return true;
}

int free_inode_tree(WOWS_BASE_INODE *inode) {
    if (inode == NULL) {
        return 0;
    }
    switch (inode->type) {
    case WOWS_INODE_TYPE_FILE:
        free(inode);
        break;
    case WOWS_INODE_TYPE_DIR:
        hashmap_scan(((WOWS_DIR_INODE *)inode)->children_inodes, iter_inode_free, NULL);
        hashmap_free(((WOWS_DIR_INODE *)inode)->children_inodes);
        free(inode);
        break;
    default:
        return WOWS_ERROR_UNKNOWN;
    }
    return 0;
}
