#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <zlib.h>
#include "wows-depack.h"
#include "wows-depack-private.h"
#include "hashmap.h"

#define CHUNK_SIZE 16384 // 16KB

int get_inode(WOWS_CONTEXT *context, char *path, WOWS_BASE_INODE **out_inode) {
    int dir_count;
    char **dirs;
    char *file;
    int ret = decompose_path(path, &dir_count, &dirs, &file);
    if (ret != 0) {
        return ret;
    }
    WOWS_DIR_INODE *inode = context->root;
    for (int i = 0; i < dir_count; i++) {
        if (inode != NULL && inode->type == WOWS_INODE_TYPE_DIR) {
            WOWS_DIR_INODE *inode_search = &(WOWS_DIR_INODE){.name = dirs[i]};
            struct hashmap *map = inode->children_inodes;
            inode = *(WOWS_DIR_INODE **)hashmap_get(map, &inode_search);
        }
        // We don't break here just to free each path items
        free(dirs[i]);
    }
    free(dirs);
    if (inode == NULL) {
        free(file);
        return WOWS_ERROR_NOT_FOUND;
    }
    WOWS_DIR_INODE *inode_search = &(WOWS_DIR_INODE){.name = file};
    struct hashmap *map = inode->children_inodes;
    inode = *(WOWS_DIR_INODE **)hashmap_get(map, &inode_search);

    free(file);
    if (inode == NULL) {
        return WOWS_ERROR_NOT_FOUND;
    }
    *out_inode = (WOWS_BASE_INODE *)inode;
    return 0;
}

int extract_file_inode(WOWS_CONTEXT *context, WOWS_FILE_INODE *file_inode, FILE *out_file) {
    if (file_inode->type != WOWS_INODE_TYPE_FILE) {
        return WOWS_ERROR_NOT_A_FILE;
    }
    uint32_t idx_index = file_inode->index_file_index;
    WOWS_INDEX *index = context->indexes[idx_index];
    char *pkg_file_path;
    get_pkg_filepath(index, &pkg_file_path);

    FILE *fd_pkg = fopen(pkg_file_path, "r");
    if (!fd_pkg) {
        wows_set_error_details(context, "error with opening pkg file '%s'", pkg_file_path);
        free(pkg_file_path);
        return WOWS_ERROR_FILE_OPEN_FAILURE;
    }
    free(pkg_file_path);

    // TODO we probably have other stuff than zlib/deflate (probably controlled by entry->type_1 and or entry->type_2)
    z_stream stream = {0};
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = 0;
    stream.next_in = Z_NULL;
    inflateInit2(&stream, -15);

    WOWS_INDEX_DATA_FILE_ENTRY *entry_search = &(WOWS_INDEX_DATA_FILE_ENTRY){.metadata_id = file_inode->id};
    void *res = hashmap_get(context->file_map, &entry_search);
    if (res == NULL) {
        return WOWS_ERROR_UNKNOWN;
    }
    WOWS_INDEX_DATA_FILE_ENTRY *entry = *(WOWS_INDEX_DATA_FILE_ENTRY **)res;

    int ret = 0;
    char *compressed_data = malloc(CHUNK_SIZE);
    char *uncompressed_data = malloc(CHUNK_SIZE);
    uint64_t offset = entry->offset_pkg_data;

    fseek(fd_pkg, offset, SEEK_SET);

    // TODO count the number of bytes we read and actually check it against entry->size_pkg_data
    do {
        // Read a chunk of compressed data from the input file
        const size_t compressed_bytes_read = fread(compressed_data, 1, CHUNK_SIZE, fd_pkg);
        // TODO also stop when the bytes counter reaches entry->size_pkg_data
        if (compressed_bytes_read == 0 && feof(fd_pkg)) {
            break; // Reached end of the data chunk input file
        }
        if (ferror(fd_pkg)) {
            ret = WOWS_ERROR_CORRUPTED_FILE;
            break;
        }

        // Decompress the chunk of data
        stream.next_in = (Bytef *)compressed_data;
        stream.avail_in = (uInt)compressed_bytes_read;
        do {
            stream.next_out = (Bytef *)uncompressed_data;
            stream.avail_out = CHUNK_SIZE;
            inflate(&stream, Z_NO_FLUSH);

            // Write the decompressed data to the output file
            const size_t uncompressed_bytes_written = CHUNK_SIZE - stream.avail_out;
            if (fwrite(uncompressed_data, 1, uncompressed_bytes_written, out_file) != uncompressed_bytes_written) {
                ret = WOWS_ERROR_FILE_WRITE;
                break;
            }
        } while (stream.avail_out == 0);
    } while (ret == 0);
    fclose(fd_pkg);
    free(compressed_data);
    free(uncompressed_data);
    inflateEnd(&stream);
    return 0;
}

int wows_extract_file_fp(WOWS_CONTEXT *context, char *file_path, FILE *dest) {
    WOWS_BASE_INODE *inode;
    int ret = get_inode(context, file_path, &inode);
    if (ret != 0) {
        return ret;
    }
    extract_file_inode(context, (WOWS_FILE_INODE *)inode, dest);
    return 0;
}