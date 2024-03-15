#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 500
// TODO clean-up this mess
#include <string.h>
#include <stddef.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <libgen.h>
#include <linux/limits.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <endian.h>

#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#include <fcntl.h>
#include <io.h>
#define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#define SET_BINARY_MODE(file)
#endif

#include "wows-depack.h"
#include "wows-depack-private.h"
#include "hashmap.h"

int get_pkg_filepath(WOWS_INDEX *index, char **out) {
    char *pkg_file_name = index->footer->_file_name;
    const int num_parents = 4;

    char *current_path = strdup(index->index_file_path);

    for (int i = 0; i < num_parents; i++) {
        current_path = dirname(current_path);
    }

    const char *fixed_path = "/res_packages/";
    char *out_path = calloc(strlen(current_path) + strlen(fixed_path) + strlen(pkg_file_name) + 1, sizeof(char));
    sprintf(out_path, "%s%s%s", current_path, fixed_path, pkg_file_name);

    free(current_path);
    *out = out_path;
    return 0;
}

// Context init function
WOWS_CONTEXT *wows_init_context(uint8_t debug_level) {
    WOWS_CONTEXT *context = calloc(sizeof(WOWS_CONTEXT), 1);
    context->metadata_map =
        hashmap_new(sizeof(WOWS_INDEX_METADATA_ENTRY *), 0, 0, 0, metadata_hash, metadata_compare, NULL, NULL);
    context->file_map = hashmap_new(sizeof(WOWS_INDEX_METADATA_ENTRY *), 0, 0, 0, file_hash, file_compare, NULL, NULL);
    context->debug_level = debug_level;
    context->root = init_root_inode();
    return context;
}

uint32_t datatoh32(char *data, size_t offset, WOWS_CONTEXT *context) {
    uint32_t *ret = (uint32_t *)(data + offset);
    if (context->is_le) {
        return le32toh(*ret);
    } else {
        return be32toh(*ret);
    }
}

uint64_t datatoh64(char *data, size_t offset, WOWS_CONTEXT *context) {
    uint64_t *ret = (uint64_t *)(data + offset);
    if (context->is_le) {
        return le64toh(*ret);
    } else {
        return be64toh(*ret);
    }
}

// Map the different section of the index file content to an index struct
int map_index_file(char *contents, size_t length, WOWS_INDEX **index_in, WOWS_CONTEXT *context) {
    WOWS_INDEX *index = calloc(sizeof(WOWS_INDEX), 1);
    *index_in = index;
    index->start_address = contents;
    index->end_address = contents + length;
    index->length = length;

    // Check header section boundaries
    returnOutIndex((char *)contents, contents + SIZE_WOWS_INDEX_HEADER, index);

    // Allocate an header section
    WOWS_INDEX_HEADER *header = (WOWS_INDEX_HEADER *)calloc(sizeof(WOWS_INDEX_HEADER), 1);
    index->header = header;

    strncpy(header->magic, contents, 4);
    // Check that the magic do match
    if (strncmp(header->magic, "ISFP", 4) != 0) {
        return WOWS_ERROR_BAD_MAGIC;
    }

    // Recover the endianess marker and set endianess in context
    memcpy(&(header->endianess), contents + 4, sizeof(uint32_t));
    context->is_le = (header->endianess == 0x2000000);

    // Extract the header data
    header->id = datatoh32(contents, 8, context);
    header->unknown_2 = datatoh32(contents, 12, context);
    header->file_dir_count = datatoh32(contents, 16, context);
    header->file_count = datatoh32(contents, 20, context);
    header->unknown_3 = datatoh32(contents, 24, context);
    header->header_size = datatoh64(contents, 32, context);
    header->offset_idx_data_section = datatoh64(contents, 40, context);
    header->offset_idx_footer_section = datatoh64(contents, 48, context);

    if (header->file_dir_count > WOWS_FILE_DIR_MAX) {
        return WOWS_ERROR_MAX_FILE;
    }

    if (header->file_count > WOWS_FILE_DIR_MAX) {
        return WOWS_ERROR_MAX_FILE;
    }

    // Get the start of the metadata array
    char *metadata_section = (contents + SIZE_WOWS_INDEX_HEADER);
    // Check the metadata section
    returnOutIndex(metadata_section, metadata_section + header->file_dir_count * SIZE_WOWS_INDEX_METADATA_ENTRY, index);

    WOWS_INDEX_METADATA_ENTRY *metadata = calloc(sizeof(WOWS_INDEX_METADATA_ENTRY), header->file_dir_count);
    index->metadata = metadata;
    for (int i = 0; i < header->file_dir_count; i++) {
        size_t offset = i * SIZE_WOWS_INDEX_METADATA_ENTRY;
        metadata[i].file_name_size = datatoh64(metadata_section + offset, 0, context);
        metadata[i].offset_idx_file_name = datatoh64(metadata_section + offset, 8, context);
        metadata[i].id = datatoh64(metadata_section + offset, 16, context);
        metadata[i].parent_id = datatoh64(metadata_section + offset, 24, context);
        if (metadata[i].file_name_size > WOWS_PATH_MAX) {
            return WOWS_ERROR_PATH_TOO_LONG;
        }

        char *file_name = calloc(sizeof(char), metadata[i].file_name_size);
        char *file_name_src = metadata_section + offset + metadata[i].offset_idx_file_name;
        returnOutIndex(file_name_src, file_name_src + metadata[i].file_name_size, index);
        strncpy(file_name, file_name_src, metadata[i].file_name_size);
        metadata[i]._file_name = file_name;
    }

    // Get the start pkg data pointer section
    char *data_file_entry_section = (contents + header->offset_idx_data_section + MAGIC_SECTION_OFFSET);

    // Check data_file_entries section boundaries
    returnOutIndex(data_file_entry_section,
                   data_file_entry_section + header->file_count * SIZE_WOWS_INDEX_DATA_FILE_ENTRY, index);

    WOWS_INDEX_DATA_FILE_ENTRY *data_file_entry =
        (WOWS_INDEX_DATA_FILE_ENTRY *)calloc(sizeof(WOWS_INDEX_DATA_FILE_ENTRY), header->file_count);
    index->data_file_entry = data_file_entry;

    for (int i = 0; i < header->file_count; i++) {
        size_t offset = i * SIZE_WOWS_INDEX_DATA_FILE_ENTRY;
        data_file_entry[i].metadata_id = datatoh64(data_file_entry_section + offset, 0, context);
        data_file_entry[i].footer_id = datatoh64(data_file_entry_section + offset, 8, context);
        data_file_entry[i].offset_pkg_data = datatoh64(data_file_entry_section + offset, 16, context);
        data_file_entry[i].type_1 = datatoh32(data_file_entry_section + offset, 24, context);
        data_file_entry[i].type_2 = datatoh32(data_file_entry_section + offset, 28, context);
        data_file_entry[i].size_pkg_data = datatoh32(data_file_entry_section + offset, 32, context);
        data_file_entry[i].id_pkg_data = datatoh64(data_file_entry_section + offset, 36, context);
    }

    // Get the footer section
    char *footer_src = (contents + header->offset_idx_footer_section + MAGIC_SECTION_OFFSET);
    // Check footer section boundaries
    returnOutIndex((char *)footer_src, (char *)footer_src + SIZE_WOWS_INDEX_FOOTER, index);

    WOWS_INDEX_FOOTER *footer = calloc(sizeof(WOWS_INDEX_FOOTER), 1);
    index->footer = footer;

    footer->pkg_file_name_size = datatoh64(footer_src, 0, context);
    footer->unknown_7 = datatoh64(footer_src, 8, context);
    footer->id = datatoh64(footer_src, 16, context);
    if (footer->pkg_file_name_size > WOWS_PATH_MAX) {
        return WOWS_ERROR_PATH_TOO_LONG;
    }

    footer->_file_name = calloc(sizeof(char), footer->pkg_file_name_size);
    char *file_name_src = footer_src + SIZE_WOWS_INDEX_FOOTER;
    returnOutIndex(file_name_src, file_name_src + footer->pkg_file_name_size, index);
    strncpy(footer->_file_name, file_name_src, footer->pkg_file_name_size);
    return 0;
}

// Add the parsed index to the context list
uint32_t add_index_context(WOWS_CONTEXT *context, WOWS_INDEX *index) {
    context->index_count++;
    int new_indice = context->index_count - 1;
    context->indexes = realloc(context->indexes, context->index_count * sizeof(WOWS_INDEX *));
    context->indexes[new_indice] = index;
    return new_indice;
}

// Get the dir/subdir path of a given file entry
int get_path(WOWS_CONTEXT *context, WOWS_INDEX_METADATA_ENTRY *mentry, int *depth,
             WOWS_INDEX_METADATA_ENTRY **entries) {
    struct hashmap *map = context->metadata_map;
    int level = 0;
    *depth = level;

    WOWS_INDEX_METADATA_ENTRY *mentry_search;

    WOWS_INDEX_METADATA_ENTRY *m_parent_entry_search = &(WOWS_INDEX_METADATA_ENTRY){.id = mentry->parent_id};
    WOWS_INDEX_METADATA_ENTRY **mparent_entry = (WOWS_INDEX_METADATA_ENTRY **)hashmap_get(map, &m_parent_entry_search);

    while (mparent_entry != NULL && level < WOWS_DIR_MAX_LEVEL) {
        entries[level] = *mparent_entry;
        mentry_search = &(WOWS_INDEX_METADATA_ENTRY){.id = (*mparent_entry)->parent_id};
        mparent_entry = NULL;
        mparent_entry = (WOWS_INDEX_METADATA_ENTRY **)hashmap_get(map, &mentry_search);
        level++;
    }
    *depth = level;
    return 0;
}

// Parse all the index files in a given directory to construct the full tree
int wows_parse_index_dir(const char *path, WOWS_CONTEXT *context) {
    int ret = 0;
    DIR *directory = opendir(path);
    if (directory == NULL) {
        wows_set_error_details(context, "error with directory '%s'", path);
        return WOWS_ERROR_FILE_OPEN_FAILURE;
    }

    struct dirent *entry;
    struct stat statbuf;
    char *full_path = NULL;
    while ((entry = readdir(directory)) != NULL) {
        // Construct the full path to the file/directory
        size_t path_len = strlen(path);
        size_t name_len = strlen(entry->d_name);
        full_path = realloc(full_path, path_len + 1 + name_len + 1);
        if (full_path == NULL) {
            return WOWS_ERROR_UNKNOWN;
        }
        snprintf(full_path, path_len + 1 + name_len + 1, "%s/%s", path, entry->d_name);

        // Get information about the file/directory
        if (lstat(full_path, &statbuf) == -1) {
            continue;
        }

        // Check if it's a regular file
        if (S_ISREG(statbuf.st_mode)) {
            wows_parse_index_file(full_path, context);
            if (ret != 0) {
                return ret;
            }
        }
    }

    free(full_path);
    closedir(directory);
    return 0;
}

// Parse an individual index file
int wows_parse_index_file(const char *index_file_path, WOWS_CONTEXT *context) {
    // Open the index file
    int fd = open(index_file_path, O_RDONLY);
    if (fd <= 0) {
        wows_set_error_details(context, "error with file '%s', %s", index_file_path, strerror(errno));
        return WOWS_ERROR_FILE_OPEN_FAILURE;
    }

    // Recover the file size
    struct stat s;
    fstat(fd, &s);

    /* index content size */
    size_t length = s.st_size;

    // Map the whole content in memory
    char *content = mmap(0, length, PROT_READ, MAP_PRIVATE, fd, 0);
    return wows_parse_index_buffer(content, length, index_file_path, fd, context);
}

// Parse a buffer directly
int wows_parse_index_buffer(char *contents, size_t length, const char *index_file_path, int fd, WOWS_CONTEXT *context) {
    int i;

    WOWS_INDEX *index;
    int err = map_index_file(contents, length, &index, context);
    if (err != 0) {
        wows_free_index(index, 0);
        return err;
    }

    // Copy over the file_path string
    char *resolved_path = NULL;

    index->index_file_path = realpath(index_file_path, resolved_path);
    index->fd = fd;

    // Debugging output if necessary
    if (context->debug_level & WOWS_DEBUG_RAW_RECORD) {
        print_debug_raw(index);
    }
    if (context->debug_level & WOWS_DEBUG_FILE_LISTING) {
        print_debug_files(index, context->metadata_map);
    }

    // Index all the metadata entries into an hmap
    for (i = 0; i < index->header->file_dir_count; i++) {
        WOWS_INDEX_METADATA_ENTRY *entry = &index->metadata[i];
        // Check that we can correctly recover the file names
        hashmap_set(context->metadata_map, &entry);
    }
    // Do the same for all the file entries
    for (int i = 0; i < index->header->file_count; i++) {
        WOWS_INDEX_DATA_FILE_ENTRY *fentry = &index->data_file_entry[i];
        hashmap_set(context->file_map, &fentry);
    }

    // Check that we can correctly recover the file names

    uint32_t current_index_context = add_index_context(context, index);

    err = build_inode_tree(index, current_index_context, context);
    return err;
}
