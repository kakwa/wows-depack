#include <stdint.h>
#include <stdbool.h>
#include <pcre.h>
#include "hashmap.h"

/* Limits */

// Maximum number of directories in a path (protection against infinite loops)
// In practice, the observed maximum depth is 7. 32 should be more than enough
#define WOWS_DIR_MAX_LEVEL 32
// Put some limits on the file name size
#define WOWS_PATH_MAX 4096

#define WOWS_FILE_DIR_MAX 32 * 1024 * 1024
/* ---------- */

/* Errors */

#define returnOutIndex(start, end, index)                                                                              \
    if (checkOutOfIndex(start, end, index)) {                                                                          \
        return WOWS_ERROR_CORRUPTED_FILE;                                                                              \
    }

/* ---------- */

/* Structures describing the file format */

#define MAGIC_SECTION_OFFSET sizeof(uint32_t) * 4

// INDEX file header
typedef struct {
    char magic[4];
    uint32_t endianess;
    uint32_t id;
    uint32_t unknown_2;
    uint32_t file_dir_count;
    uint32_t file_count;
    uint64_t unknown_3;
    uint64_t header_size;
    uint64_t offset_idx_data_section;
    uint64_t offset_idx_footer_section;
} WOWS_INDEX_HEADER;

#define SIZE_WOWS_INDEX_HEADER 56

// INDEX file metadata entry
typedef struct {
    uint64_t file_name_size;
    uint64_t offset_idx_file_name;
    uint64_t id;
    uint64_t parent_id;
    char *_file_name;
} WOWS_INDEX_METADATA_ENTRY;

#define SIZE_WOWS_INDEX_METADATA_ENTRY 32

// INDEX file pkg data pointer entry
typedef struct {
    uint64_t metadata_id;
    uint64_t footer_id;
    uint64_t offset_pkg_data;
    uint32_t type_1;
    uint32_t type_2;
    uint32_t size_pkg_data;
    uint64_t id_pkg_data;
    uint32_t padding;
} WOWS_INDEX_DATA_FILE_ENTRY;

#define SIZE_WOWS_INDEX_DATA_FILE_ENTRY 48

// INDEX file footer
typedef struct {
    uint64_t pkg_file_name_size;
    uint64_t unknown_7;
    uint64_t id;
    char *_file_name;
} WOWS_INDEX_FOOTER;

#define SIZE_WOWS_INDEX_FOOTER 24

// PKG file ID + padding
typedef struct {
    uint32_t padding_1;
    uint64_t id;
    uint32_t padding_2;
} WOWS_PKG_ID_ENTRY;

/* ---------- */

/* Simple Inode representation of the tree */

#define WOWS_ROOT_INODE 0

#define WOWS_INODE_TYPE_DIR 0
#define WOWS_INODE_TYPE_FILE 1

// Base inode
typedef struct {
    uint8_t type;                        // type of node (either WOWS_FILE_INODE or WOWS_DIR_INODE
    uint64_t id;                         // id of the corresponding metadata item
    uint32_t index_file_index;           // index of the index in WOWS_CONTEXT.indexes
    char *name;                          // Name of the file/dir
    struct WOWS_DIR_INODE *parent_inode; // parent inode (always a directory)
} WOWS_BASE_INODE;

// Dir inode
typedef struct WOWS_DIR_INODE {
    uint8_t type;                        // always WOWS_DIR_INODE
    uint64_t id;                         // id of the corresponding metadata item
    uint32_t index_file_index;           // index of the index in WOWS_CONTEXT.indexes
    char *name;                          // Name of the file/dir
    struct WOWS_DIR_INODE *parent_inode; // parent inode (always a directory)
    struct hashmap *children_inodes;     // children inodes (directories or files)
} WOWS_DIR_INODE;

// file inode
typedef struct WOWS_FILE_INODE {
    uint8_t type;                        // always WOWS_FILE_INODE
    uint64_t id;                         // id of the corresponding metadata item
    uint32_t index_file_index;           // context index of the index file
    char *name;                          // Name of the file/dir
    struct WOWS_DIR_INODE *parent_inode; // parent inode (always a directory)
} WOWS_FILE_INODE;

// Index File structure
typedef struct WOWS_INDEX {
    WOWS_INDEX_HEADER *header;
    WOWS_INDEX_METADATA_ENTRY *metadata;
    WOWS_INDEX_DATA_FILE_ENTRY *data_file_entry;
    WOWS_INDEX_FOOTER *footer;
    char *start_address;
    char *end_address;
    size_t length;
    int fd;
    char *index_file_path;
} WOWS_INDEX;

typedef struct {
    WOWS_CONTEXT *context;
    FILE *pkg_fp;
    FILE *idx_fp;
    WOWS_INDEX *index;
    char *filename_section;
    uint64_t filename_section_offset;
    uint64_t filename_section_size;
    uint64_t file_plus_dir_count;
    uint64_t metadata_section_size;
    uint64_t file_count;
    uint64_t file_section_size;
    uint64_t footer_id;
    uint64_t pkg_cur_offset;
} wows_writer;

/* ---------- */

bool checkOutOfIndex(char *start, char *end, WOWS_INDEX *index);
int wows_free_index(WOWS_INDEX *index, int flag);
int wows_parse_index(char *index_file_path, WOWS_CONTEXT *context);
int map_index_file(char *contents, size_t length, WOWS_INDEX **index_in, WOWS_CONTEXT *context);
char *wows_render_str(char *fmt, ...);
void wows_set_error_details(WOWS_CONTEXT *context, char *fmt, ...);
int print_header(WOWS_INDEX_HEADER *header);
int print_footer(WOWS_INDEX_FOOTER *footer);
int print_metadata_entry(WOWS_INDEX_METADATA_ENTRY *entry);
int print_data_file_entry(WOWS_INDEX_DATA_FILE_ENTRY *entry);
int print_debug_files(WOWS_INDEX *index, struct hashmap *map);
int print_debug_raw(WOWS_INDEX *index);
int print_inode_tree(WOWS_BASE_INODE *inode, int level);
int print_inode_flat(WOWS_BASE_INODE *inode);
int free_inode_tree(WOWS_BASE_INODE *inode);
bool iter_inode_free(const void *item, void *udata);
bool dir_inode_print(const void *item, void *udata);
WOWS_BASE_INODE *get_child(WOWS_DIR_INODE *inode, char *name);
int get_path(WOWS_CONTEXT *context, WOWS_INDEX_METADATA_ENTRY *mentry, int *depth, WOWS_INDEX_METADATA_ENTRY **entries);
int metadata_compare(const void *a, const void *b, void *udata);
uint64_t metadata_hash(const void *item, uint64_t seed0, uint64_t seed1);
int file_compare(const void *a, const void *b, void *udata);
uint64_t file_hash(const void *item, uint64_t seed0, uint64_t seed1);
int dir_inode_compare(const void *a, const void *b, void *udata);
uint64_t dir_inode_hash(const void *item, uint64_t seed0, uint64_t seed1);
int build_inode_tree(WOWS_INDEX *index, int current_index_context, WOWS_CONTEXT *context);
WOWS_DIR_INODE *init_root_inode();
int add_child_inode(WOWS_DIR_INODE *parent_inode, WOWS_BASE_INODE *inode);
WOWS_BASE_INODE *get_child(WOWS_DIR_INODE *inode, char *name);
int get_pkg_filepath(WOWS_INDEX *index, char **out);
WOWS_DIR_INODE *init_dir_inode(uint64_t metadata_id, uint32_t current_index_context, WOWS_DIR_INODE *parent_inode,
                               WOWS_CONTEXT *context);
WOWS_FILE_INODE *init_file_inode(uint64_t metadata_id, uint32_t current_index_context, WOWS_DIR_INODE *parent_inode,
                                 WOWS_CONTEXT *context);
int get_path_inode(WOWS_BASE_INODE *inode, int *depth, char *entries[]);

/* dump an index in a file */
int wows_dump_index_to_file(WOWS_INDEX *index, FILE *f);

int decompose_path(const char *path, int *out_dir_count, char ***out_dirs, char **out_file);
char *join_path(char **parent_entries, int depth, char *name);
pcre *compile_regex(const char *pattern);
bool match_regex(pcre *re, const char *subject);
int free_regex(pcre *re);

int write_file_name(wows_writer *writer, char *name);
int write_file_pkg_entry(WOWS_INDEX_DATA_FILE_ENTRY **file_section, uint64_t *file_section_size, uint64_t metadata_id,
                         uint64_t footer_id, uint64_t offset, uint32_t size, uint64_t pkg_id, uint64_t *file_count);

int write_data_blob(char *file_path, FILE *pkg_fp, uint64_t *offset, uint32_t *size_written, uint64_t pkg_id);

int write_metadata_entry(wows_writer *writer, uint64_t metadata_id, uint64_t parent_id, uint64_t offset_idx_file_name,
                         char *file_name);
int recursive_writer(wows_writer *writer, char *path, uint64_t parent_id);
int copy_data(FILE *in, FILE *out, long offset, size_t size);
int internal_wows_extract_dir(WOWS_CONTEXT *context, char *dir_path, char *out_dir_path, FILE *magic_fp);
FILE *open_file_with_parents(const char *filename);
