/**
 * @file wows-depack.h
 *
 * @brief Header file for the wows-depack library.
 */

#include <stdint.h>
#include <stdbool.h>

/* Error codes  */

#define WOWS_ERROR_CORRUPTED_FILE 1             /**< A corrupted WoWs resource file. */
#define WOWS_ERROR_BAD_MAGIC 2                  /**< A bad magic number in a WoWs resource file. */
#define WOWS_ERROR_MISSING_METADATA_ENTRY 3     /**< A missing metadata entry in a WoWs resource file. */
#define WOWS_ERROR_MAX_LEVEL_REACHED 4          /**< The maximum directory nesting level has been reached. */
#define WOWS_ERROR_NON_ZERO_TERMINATED_STRING 5 /**< A non-zero-terminated string in a WoWs resource file. */
#define WOWS_ERROR_PATH_TOO_LONG 6              /**< A path is too long to be processed. */
#define WOWS_ERROR_UNKNOWN 7                    /**< An unknown error occurred. */
#define WOWS_ERROR_ID_COLLISION_FILE_DIR 8      /**< A file and directory identifier collision. */
#define WOWS_ERROR_FILE_OPEN_FAILURE 9          /**< A failure to open a file. */
#define WOWS_ERROR_DECOMPOSE_PATH 10            /**< An error while decomposing a path. */
#define WOWS_ERROR_INVALID_SEARCH_PATTERN 11    /**< failure to compile regex pattern */
#define WOWS_ERROR_NOT_A_FILE 12                /**< path is not a file */
#define WOWS_ERROR_NOT_A_DIR 13                 /**< path is not a directory */
#define WOWS_ERROR_NOT_FOUND 14                 /**< file or directory not found */
#define WOWS_ERROR_FILE_WRITE 15                /**< file write error */
#define WOWS_ERROR_MAX_FILE 16                  /**< maximum number of file/dir */

/* ---------- */

/* Debug levels */

#define WOWS_NO_DEBUG 0                  /**< No debug output. */
#define WOWS_DEBUG_RAW_RECORD (1 << 0)   /**< Debug output for raw records. */
#define WOWS_DEBUG_FILE_LISTING (1 << 1) /**< Debug output for file listings. */

/* ---------- */

/*  Search Modes */

#define WOWS_SEARCH_FILE_ONLY 0     /**< Search only on file names. */
#define WOWS_SEARCH_DIR_ONLY 1      /**< Search only on directory names. */
#define WOWS_SEARCH_FILE_PLUS_DIR 2 /**< Search on directory and file names. */
#define WOWS_SEARCH_FULL_PATH 3     /**< Search on the full path of files. */

/* ---------- */

/*
 * @brief WoWs resource extractor context.
 *
 * This structure is used to hold the context for the WoWs extractor.
 *
 * This structure is not meant to be manipulated directly.
 * Internal fields are private and could be subject to changes.
 */
typedef struct {
    uint8_t debug_level;  // Debug level for logging
    void *root;           // Root Inode (WOWS_DIR_INODE * root;)
    void *metadata_map;   // Global Metadata hashmap (struct hashmap *metadata_map;)
    void *file_map;       // Global File hashmap (struct hashmap *file_map;)
    void *current_dir;    // Current directory (WOWS_DIR_INODE *current_dir;)
    void **indexes;       // Array of structures representing each index file (WOWS_INDEX
                          // **indexes;)
    uint32_t index_count; // Size of the array
    bool is_le;           // Flag for endianess (true if LE, false if BE)
    char *err_msg;        // Last error message
} WOWS_CONTEXT;

/* ---------- */

/* Context init/destruction */

/**
 * @brief Initializes a WoWs resource extractor context.
 *
 * This function creates and initializes a new WoW Stats Parser context with the given
 * debug level.
 *
 * @param debug_level The debug level for the parser.
 *
 * @return A pointer to the newly created context, or NULL if an error occurred.
 *
 * @note Call wows_free_context() to liberate context memory
 */
WOWS_CONTEXT *wows_init_context(uint8_t debug_level);

/**
 * @brief Frees a WoWs resource extractor context.
 *
 * This function frees the memory allocated for a WoWs resource extractor context.
 *
 * @param context A pointer to the context to free.
 *
 * @return 0 on success, or an error code if an error occurred.
 */
int wows_free_context(WOWS_CONTEXT *context);

/**
 * @brief Frees a WoW Stats Parser context without unmapping the memory.
 *
 * This function frees the memory allocated for a WoWs resource extractor context, but does not
 * unmap the memory or close files from the process address space. This function should only be used
 * if the raw `wows_parse_index_buffer` function was used.
 *
 * @param context A pointer to the context to free.
 *
 * @return 0 on success, or an error code if an error occurred.
 */
int wows_free_context_no_munmap(WOWS_CONTEXT *context);
/* ---------- */

/* Index parsing functions */

/**
 * @brief Parses a single WoWs resource index file.
 *
 * This function parses a single WoWs resource index file with the given path and updates
 * the given parser context with the parsed data.
 *
 * @param index_file_path The path to the WoWs resource index file to parse.
 * @param context A pointer to the parser context to update.
 *
 * @return 0 on success, or an error code if an error occurred.
 */
int wows_parse_index_file(const char *index_file_path, WOWS_CONTEXT *context);

/**
 * @brief Parses all files in a given directory.
 *
 * This function parses all WoWs resource index files in a given directory with the
 * given path and updates the given parser context with the parsed data.
 *
 * @param index_dir_path The path to the directory containing the WoWs resource index files to parse.
 * @param context A pointer to the parser context to update.
 *
 * @return 0 on success, or an error code if an error occurred.
 */
int wows_parse_index_dir(const char *index_dir_path, WOWS_CONTEXT *context);

/**
 * @brief Low level, parses a memory buffer directly.
 *
 * This function parses a WoWs resource index buffer directly from memory with the given
 * contents and length, and updates the given parser context with the parsed data.
 *
 * @param contents A pointer to the buffer containing the WoWs resource index contents to parse.
 * @param length The length of the buffer in bytes.
 * @param index_file_path The path to the WoWs resource index file.
 * @param fd The file descriptor associated with the index file.
 * @param context A pointer to the parser context to update.
 *
 * @return 0 on success, or an error code if an error occurred.
 */
int wows_parse_index_buffer(char *contents, size_t length, const char *index_file_path, int fd, WOWS_CONTEXT *context);

/* Archive exploration functions */

/**
 * @brief Recursively searches for files in the WOWS archive tree that match a PCRE regular expression pattern.
 *
 * @param context The WOWS_CONTEXT object representing the root of the archive tree.
 * @param pattern The PCRE regular expression pattern to match against file names.
 *             The pattern is passed to the matcher as ^{PATTERN}$
 * @param mode The search mode, which can be one of the following values:
 *             - WOWS_SEARCH_FILE_ONLY: search for files only (exclude directories)
 *             - WOWS_SEARCH_DIR_ONLY:  search for directories only (exclude files)
 *             - WOWS_SEARCH_FILE_PLUS_DIR:  search on directory and file names.
 *             - WOWS_SEARCH_FULL_PATH: search on the full file pathes
 * @param[out] result_count A pointer to an integer variable that will be set to the number of matching files or
 * directories found.
 * @param[out] results A pointer to an array of strings that will be allocated to hold the names of the matching files
 * or directories. The caller is responsible for freeing the memory allocated for this array and its elements.
 * @return 0 on success, or a non-zero error code on failure.
 *
 * This function searches recursively through the WOWS archive tree for files or directories that match the given PCRE
 * regular expression pattern. The search can be restricted to files, directories, or the full path, depending on the
 * value of the `mode` parameter. The names of the matching files or directories are stored in the `results` array, and
 * the number of matches is stored in the `result_count` variable. The caller is responsible for freeing the memory
 * allocated for the `results` array and its elements.
 *
 * @return 0 on success, or an error code if an error occurred.
 *
 * @note It's up to the caller to free each entry in the results array and free the array itself.
 *
 */
int wows_search(WOWS_CONTEXT *context, char *pattern, int mode, int *result_count, char ***results);

/* read the content of a directory */
// Not implemented
// int wows_readdir(WOWS_CONTEXT *context, char dir_path, char **result[]);

// Not implemented
/* give the stat of a given path (mainly if it's a directory or a file, and the file size) */
// int wows_stat_path(WOWS_CONTEXT *context, char path);

/* ---------- */

/* Archive extraction functions */

/**
 * @brief Extract a given file from the archive (file pointer version).
 *
 * @param context  Pointer to a WOWS_CONTEXT structure.
 * @param file_path  Path of the file to extract.
 * @param output  Pointer to a FILE structure to write the output to.
 *
 * @return 0 on success, or an error code if an error occurred.
 *
 * @note It's up to the caller to open and close FILE *output
 */
int wows_extract_file_fp(WOWS_CONTEXT *context, char *file_path, FILE *output);

/**
 * @brief Extract a given file from the archive (output file version).
 *
 *
 * @param context  Pointer to a WOWS_CONTEXT structure.
 * @param file_path  Path of the file to extract.
 * @param out_path  Path to the output file.
 *
 * @return 0 on success, or an error code if an error occurred.
 */
int wows_extract_file(WOWS_CONTEXT *context, char *file_path, char *out_path);

/**
 * @brief Extract a given directory (recursively) from the archive (output file version).
 *
 * This function will extract all files under the given directory.
 * The whole directory tree from the archive will be recreated under out_dir_path.
 *
 *
 * @param context  Pointer to a WOWS_CONTEXT structure.
 * @param dir_path  Path of the directory to extract.
 * @param out_dir_path  Path to the output directory.
 *
 * @return 0 on success, or an error code if an error occurred.
 */
int wows_extract_dir(WOWS_CONTEXT *context, char *dir_path, char *out_dir_path);

/* ---------- */

/* Debugging/output functions */

/**
 * @brief Prints the directory tree of the given WoWs resource parser context in a tree format.
 *
 * This function prints the directory tree of the given WoWs resource parser context in a tree format.
 *
 * @param context A pointer to the WoWs resource parser context to print.
 *
 * @return 0 on success, or an error code if an error occurred.
 */
int wows_print_tree(WOWS_CONTEXT *context);

/**
 * @brief Prints the directory tree of the given WoWs resource parser context in a flat format.
 *
 * This function prints the directory tree of the given WoWs resource parser context in a flat format (each file with
 * its full path).
 *
 * For example:
 * /dir1/file2
 * /dir3/dir4/file5
 *
 *
 * @param context A pointer to the WoWs resource parser context to print.
 *
 * @return 0 on success, or an error code if an error occurred.
 */
int wows_print_flat(WOWS_CONTEXT *context);

/**
 * @brief Converts an error code into an error message string for the given WoWs resource parser context.
 *
 * This function converts an error code into an error message string for the given WoWs resource parser context.
 *
 * @param error_code The error code to convert.
 * @param context A pointer to the WoWs resource parser context to use.
 *
 * @return 0 on success, or an error code if an error occurred.
 *
 * @note It's up to the caller to free the returned string
 */
char *wows_error_string(int error_code, WOWS_CONTEXT *context);

/* ---------- */

/**
 * @brief Writes a package file and its index.
 *
 * This function takes a `WOWS_CONTEXT` pointer, an input directory path, a name for the .pkg file, and pointers to
 * file streams for the package file and its index. The function writes the package file and its index to the
 * provided file streams, using the given name.
 *
 * @param context Pointer to a `WOWS_CONTEXT` struct.
 * @param in_path Input directory path to package.
 * @param name Name for the '.pkg file'.
 * @param pkg_fp Pointer to a file stream to write the package file to.
 * @param idx_fp Pointer to a file stream to write the index file to.
 * @return 0 on success, or an error code if an error occurred.
 *
 * @note This function is experimental
 */
int wows_write_pkg(WOWS_CONTEXT *context, char *in_path, char *name, FILE *pkg_fp, FILE *idx_fp);

/**
 * @brief Gets the latest index directory in the specified WoWs installation directory.
 *
 * This function takes a base directory path for a World of Warships installation and a pointer to a char pointer to
 * store the path of the latest index directory.
 *
 * @param wows_base_dir Path to the World of Warships installation directory.
 * @param idx_dir Pointer to a char pointer to store the name of the latest index directory. The memory for this
 * pointer will be allocated by the function and must be freed by the caller.
 * @return 0 on success, or an error code if an error occurred.
 *
 * @note The caller is responsible for freeing the memory allocated for the idx_dir parameter.
 */
int wows_get_latest_idx_dir(char *wows_base_dir, char **idx_dir);
