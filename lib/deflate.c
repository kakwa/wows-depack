#define _POSIX_C_SOURCE 200809L
// TODO clean-up this mess
#include <string.h>
#include <stddef.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <zlib.h>

#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#include <fcntl.h>
#include <io.h>
#define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#define SET_BINARY_MODE(file)
#endif

#define CHUNK_NAME_SECTION 128
#define CHUNK_FILE 10
#define CHUNK_SIZE 16384 // 16KB

#include "wows-depack.h"
#include "wows-depack-private.h"
#include "hashmap.h"

uint64_t random_uint64() {
    uint64_t value = 0;

    // Generate 4 random 16-bit values and combine them into a 64-bit value
    for (int i = 0; i < 4; i++) {
        uint16_t random_value = rand();
        value |= ((uint64_t)random_value << (i * 16));
    }

    return value;
}

/**
    Appends a file name to the input buffer at the given offset.
*/
int write_file_name(wows_writer *writer, char *name) {
    char *input_buffer = writer->filename_section;
    size_t offset = writer->filename_section_offset;
    size_t current_size = writer->filename_section_size;
    size_t len = strlen(name) + 1;

    char *new_buffer = calloc(sizeof(char), current_size + len);
    memcpy(new_buffer, input_buffer, current_size);
    free(writer->filename_section);
    writer->filename_section = new_buffer;
    writer->filename_section_size = current_size + len;
    memcpy(writer->filename_section + offset, name, len);
    writer->filename_section_offset += len;
    return 0;
}

/**

    Writes an entry for a file in a World of Warships (WoWS) package file section.

    @param file_section A pointer to a pointer to an array of WOWS_INDEX_DATA_FILE_ENTRY structs representing the file
   section.
    @param file_section_size A pointer to a 64-bit unsigned integer representing the size of the file section array.
    @param metadata_id A 64-bit unsigned integer representing the metadata ID for the file.
    @param footer_id A 64-bit unsigned integer representing the footer ID for the file.
    @param offset A 64-bit unsigned integer representing the offset of the file data within the package file.
    @param size A 32-bit unsigned integer representing the size of the file data.
    @param pkg_id A 64-bit unsigned integer representing the ID of the package containing the file.
    @param file_count A pointer to a 64-bit unsigned integer representing the number of files in the file section array.

    @return Returns 0 on success, or a non-zero value on failure.
*/
int write_file_pkg_entry(WOWS_INDEX_DATA_FILE_ENTRY **file_section, uint64_t *file_section_size, uint64_t metadata_id,
                         uint64_t footer_id, uint64_t offset, uint32_t size, uint64_t pkg_id, uint64_t *file_count) {
    if ((*file_section_size - *file_count) == 0) {
        *file_section = realloc(*file_section, sizeof(WOWS_INDEX_DATA_FILE_ENTRY) * (*file_count + CHUNK_FILE));
        (*file_section_size) += CHUNK_FILE;
    }
    (*file_section)[*file_count].metadata_id = metadata_id;
    (*file_section)[*file_count].type_1 = 0x5;
    (*file_section)[*file_count].type_2 = 0x1;
    (*file_section)[*file_count].footer_id = footer_id;
    (*file_section)[*file_count].offset_pkg_data = offset;
    (*file_section)[*file_count].size_pkg_data = size;
    (*file_section)[*file_count].id_pkg_data = pkg_id;
    (*file_section)[*file_count].padding = 0;
    (*file_count)++;
    return 0;
}

/**

    Writes a data blob to a package file, compressing it.

    @param file_path The path to the data file to write.
    @param pkg_fp A pointer to a FILE object representing the package file.
    @param offset A pointer to the offset in the package file where the data blob should be written.
    @param pkg_id Unsigned integer representing the ID of the data blob being written.

    @return Returns 0 on success, or a non-zero value on failure.
*/
int write_data_blob(char *file_path, FILE *pkg_fp, uint64_t *offset, uint32_t *size_written, uint64_t pkg_id) {
    int ret;
    unsigned char in[CHUNK_SIZE];
    unsigned char out[CHUNK_SIZE];
    z_stream strm;
    long start = ftell(pkg_fp);

    // Open the input file for reading
    FILE *input_file = fopen(file_path, "rb");
    if (input_file == NULL) {
        return WOWS_ERROR_UNKNOWN;
    }

    // Initialize the zlib stream
    memset(&strm, 0, sizeof(strm));

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        fclose(input_file);
        return WOWS_ERROR_UNKNOWN;
    }

    int flush = 0;
    unsigned have = 0;
    /* compress until end of file */
    do {
        strm.avail_in = fread(in, 1, CHUNK_SIZE, input_file);
        if (ferror(input_file)) {
            (void)deflateEnd(&strm);
            return Z_ERRNO;
        }
        flush = feof(input_file) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;
        /* run deflate() on input until output buffer not full, finish
           compression if all of source has been read in */
        do {
            strm.avail_out = CHUNK_SIZE;
            strm.next_out = out;
            ret = deflate(&strm, flush);   /* no bad return value */
            assert(ret != Z_STREAM_ERROR); /* state not clobbered */
            have = CHUNK_SIZE - strm.avail_out;
            if (fwrite(out, 1, have, pkg_fp) != have || ferror(pkg_fp)) {
                (void)deflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0); /* all input will be used */
        /* done when last data in file processed */
    } while (flush != Z_FINISH);

    long end_data = ftell(pkg_fp);
    *size_written = end_data - start;
    WOWS_PKG_ID_ENTRY entry;
    entry.padding_1 = 0;
    entry.id = pkg_id;
    entry.padding_2 = 0;
    fwrite((char *)&entry, 1, sizeof(WOWS_PKG_ID_ENTRY), pkg_fp);

    long end = ftell(pkg_fp);
    *offset = (uint64_t)end;

    // Clean up and close files
    deflateEnd(&strm);
    fclose(input_file);

    return 0;
}

/**
    Writes an entry for metadata in a World of Warships (WoWS) package metadata section.
*/

int write_metadata_entry(wows_writer *writer, uint64_t metadata_id, uint64_t parent_id, uint64_t offset_idx_file_name,
                         char *file_name) {
    uint64_t metadata_section_size = writer->metadata_section_size;
    uint64_t file_plus_dir_count = writer->file_plus_dir_count;

    // If the metadata section array is full, allocate more space for it.
    if ((metadata_section_size - file_plus_dir_count) == 0) {
        writer->index->metadata =
            realloc(writer->index->metadata, sizeof(WOWS_INDEX_METADATA_ENTRY) * (file_plus_dir_count + CHUNK_FILE));
        writer->metadata_section_size += CHUNK_FILE;
    }
    (writer->index->metadata)[file_plus_dir_count].id = metadata_id;
    (writer->index->metadata)[file_plus_dir_count].file_name_size = strlen(file_name);
    (writer->index->metadata)[file_plus_dir_count].offset_idx_file_name = offset_idx_file_name;
    (writer->index->metadata)[file_plus_dir_count].parent_id = parent_id;
    char *file_name_cpy = calloc(sizeof(char), strlen(file_name) + 1);
    strcpy(file_name_cpy, file_name);
    (writer->index->metadata)[file_plus_dir_count]._file_name = file_name_cpy;
    writer->file_plus_dir_count++;
    return 0;
}

int wows_write_pkg(WOWS_CONTEXT *context, char *in_path, char *name_pkg, FILE *pkg_fp, FILE *idx_fp) {
    WOWS_INDEX *index = calloc(sizeof(WOWS_INDEX), 1);
    wows_writer *writer = calloc(sizeof(wows_writer), 1);
    writer->index = index;

    writer->context = context;
    writer->pkg_fp = pkg_fp;
    writer->idx_fp = idx_fp;
    writer->footer_id = random_uint64();
    index->header = calloc(sizeof(WOWS_INDEX_HEADER), 1);

    // Setup footer
    index->footer = malloc(sizeof(WOWS_INDEX_FOOTER) + strlen(name_pkg) + 1);
    index->footer->pkg_file_name_size = strlen(name_pkg) + 1;
    index->footer->id = writer->footer_id;
    index->footer->unknown_7 = random_uint64(); // FIXME dubious, but we don't know what this field does.
    index->footer->_file_name = calloc(sizeof(char), strlen(name_pkg) + 1);
    memcpy(index->footer->_file_name, name_pkg, strlen(name_pkg) + 1);
    memcpy((char *)(index->footer) + sizeof(WOWS_INDEX_FOOTER), name_pkg, strlen(name_pkg) + 1);

    uint64_t parent_id = random_uint64();
    recursive_writer(writer, in_path, parent_id);
    // Recompute the offset between metadata entries and file names
    // We need to add the offset consituted by the metadata entries we have between one metadata entry and its filename
    for (int i = 0; i < writer->file_plus_dir_count; i++) {
        uint64_t added_offset = (writer->file_plus_dir_count - i) * sizeof(WOWS_INDEX_METADATA_ENTRY);
        writer->index->metadata[i].offset_idx_file_name += added_offset;
    }
    // Concatenate the metadata section and the filename section
    size_t metadata_byte_len = sizeof(WOWS_INDEX_METADATA_ENTRY) * writer->file_plus_dir_count;
    WOWS_INDEX_METADATA_ENTRY *new_metadata_section =
        calloc(metadata_byte_len + writer->filename_section_offset, sizeof(char));
    memcpy(new_metadata_section, index->metadata, metadata_byte_len);
    memcpy((char *)new_metadata_section + metadata_byte_len, writer->filename_section, writer->filename_section_offset);
    free(index->metadata);
    free(writer->filename_section);
    index->metadata = new_metadata_section;

    // Setup header
    memcpy(index->header->magic, "ISFP", 4);
    index->header->endianess = 0x2000000;
    index->header->id = (uint32_t)rand();
    index->header->unknown_2 = 0x40;
    index->header->file_dir_count = writer->file_plus_dir_count;
    index->header->file_count = writer->file_count;
    index->header->unknown_3 = 1;
    index->header->header_size = 40;
    index->header->offset_idx_data_section = 0;   // Recomputed by wows_dump_index_to_file
    index->header->offset_idx_footer_section = 0; // Same
    wows_dump_index_to_file(index, idx_fp);
    wows_free_index(index, 0);
    free(writer);
    return 0;
}

int recursive_writer(wows_writer *writer, char *path, uint64_t parent_id) {
    DIR *dir;
    struct dirent *entry;
    struct stat info;

    if ((dir = opendir(path)) == NULL) {
        return WOWS_ERROR_FILE_OPEN_FAILURE;
    }

    while ((entry = readdir(dir)) != NULL) {
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        // Ignore errors
        // FIXME even if not fatal we should do something with these ones
        if (stat(full_path, &info) != 0) {
            continue;
        }

        if (S_ISDIR(info.st_mode)) {
            // Ignore "." and ".." directories
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            // FIXME factorize (in both the dir and file case, we end-up writting a metadata entry)
            uint64_t offset_idx_file_name = writer->filename_section_offset;
            // TODO use the context to check if this entry already exists (if so we must reuse the same ID)
            uint64_t metadata_id = random_uint64();
            write_file_name(writer, entry->d_name);
            write_metadata_entry(writer, metadata_id, parent_id, offset_idx_file_name, entry->d_name);
            recursive_writer(writer, full_path, metadata_id);
        } else if (S_ISREG(info.st_mode)) {
            uint64_t offset_idx_file_name = writer->filename_section_offset;
            uint64_t metadata_id = random_uint64();
            uint64_t start_offset = writer->pkg_cur_offset;
            uint32_t size = 0;
            // Shift by 16 bits to mimic wows IDs
            uint64_t pkg_id = random_uint64() >> 16;
            write_data_blob(full_path, writer->pkg_fp, &(writer->pkg_cur_offset), &size, pkg_id);
            write_file_name(writer, entry->d_name);
            write_metadata_entry(writer, metadata_id, parent_id, offset_idx_file_name, entry->d_name);
            write_file_pkg_entry(&(writer->index->data_file_entry), &(writer->file_section_size), metadata_id,
                                 writer->footer_id, start_offset, size, pkg_id, &(writer->file_count));
        }
    }

    closedir(dir);

    return 0;
}

// Index Dumper function
int wows_dump_index_to_file(WOWS_INDEX *index, FILE *f) {
    uint64_t start_data;
    uint64_t start_footer;
    uint64_t general_offset = 0;

    // Write the header

    fwrite(index->header, sizeof(WOWS_INDEX_HEADER), 1, f);
    general_offset += sizeof(WOWS_INDEX_HEADER);

    // Write the metadata entries
    uint64_t string_start = sizeof(WOWS_INDEX_METADATA_ENTRY) * index->header->file_dir_count;
    for (size_t i = 0; i < index->header->file_dir_count; i++) {
        WOWS_INDEX_METADATA_ENTRY metadata_entry = index->metadata[i];
        metadata_entry.offset_idx_file_name = string_start;
        fwrite(&metadata_entry, sizeof(WOWS_INDEX_METADATA_ENTRY), 1, f);
        general_offset += sizeof(WOWS_INDEX_METADATA_ENTRY);
        string_start -= sizeof(WOWS_INDEX_METADATA_ENTRY);
        string_start += metadata_entry.file_name_size;
    }

    // Write the file name strings
    for (size_t i = 0; i < index->header->file_dir_count; i++) {
        WOWS_INDEX_METADATA_ENTRY *metadata_entry = &index->metadata[i];
        char *file_name = metadata_entry->_file_name;
        if (file_name != NULL && metadata_entry->file_name_size != 0)
            fwrite(file_name, metadata_entry->file_name_size, 1, f);
        general_offset += metadata_entry->file_name_size;
    }

    start_data = general_offset;
    // Write the data file entries
    for (size_t i = 0; i < index->header->file_count; i++) {
        WOWS_INDEX_DATA_FILE_ENTRY *data_file_entry = &index->data_file_entry[i];
        fwrite(data_file_entry, sizeof(WOWS_INDEX_DATA_FILE_ENTRY), 1, f);
        general_offset += sizeof(WOWS_INDEX_DATA_FILE_ENTRY);
    }

    // Write the footer
    start_footer = general_offset;
    fwrite(index->footer, sizeof(WOWS_INDEX_FOOTER), 1, f);

    // Write the pkg file name
    char *pkg_file_name = (char *)index->footer + sizeof(WOWS_INDEX_FOOTER);
    fwrite(pkg_file_name, index->footer->pkg_file_name_size, 1, f);

    // Re-Write the header with the correct offsets
    fseek(f, 0, SEEK_SET);
    WOWS_INDEX_HEADER cpy_header = *index->header;
    cpy_header.header_size = 40;
    cpy_header.unknown_2 = 0x40;
    cpy_header.offset_idx_data_section = start_data - MAGIC_SECTION_OFFSET;
    cpy_header.offset_idx_footer_section = start_footer - MAGIC_SECTION_OFFSET;
    fwrite(&cpy_header, sizeof(WOWS_INDEX_HEADER), 1, f);

    return 0;
}
