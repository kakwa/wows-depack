#define _POSIX_C_SOURCE 200809L

#include <argp.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include "inc/wows-depack.h"

#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#include <fcntl.h>
#include <io.h>
#define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#define SET_BINARY_MODE(file)
#endif

#define CHUNK 16384

const char *argp_program_version = BFD_VERSION;

const char *argp_program_bug_address = "https://github.com/wows-tools/wows-depack/issues";

static char doc[] = "\nWorld of Warships resource extractor tool";

static struct argp_option options[] = {{"input", 'i', "INPUT_INDEX", 0, "Input index file"},
                                       {"input-dir", 'I', "INPUT_INDEX_DIR", 0, "Input index directory"},
                                       {"wows-dir", 'W', "WOWS_BASE_DIR", 0, "Input World of Warships base directory"},
                                       {"output-dir", 'O', "OUTPUT_DIR", 0, "Output dir for recursive extract"},
                                       {"output", 'o', "OUTPUT_FILE", 0, "Output file when extracting one file"},
                                       {"search", 's', "SEARCH_PATTERN", 0, "Search Regex"},
                                       {"extract", 'e', "FILE_DIR_TO_EXTRACT", 0, "File or directory to extract"},
                                       {"print", 'p', NULL, 0, "Print All files"},
                                       {"debug", 'd', NULL, 0, "Debug Output"},
                                       {0}};

/* A description of the arguments we accept. */
static char args_doc[] = "<-i INPUT_FILE | -I INPUT_DIR | -W WOWS_BASE_DIR>";

struct arguments {
    char *args[2]; /* arg1 & arg2 */
    char *output;
    char *output_dir;
    char *wows_base_dir;
    char *extract;
    char *search;
    char *input;
    char *input_dir;
    bool print;
    bool debug;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    /* Get the input argument from argp_parse, which we
       know is a pointer to our arguments structure. */
    struct arguments *arguments = (struct arguments *)state->input;

    switch (key) {
    case 'i':
        arguments->input = arg;
        break;
    case 'I':
        arguments->input_dir = arg;
        break;
    case 'W':
        arguments->wows_base_dir = arg;
        break;
    case 'o':
        arguments->output = arg;
        break;
    case 'O':
        arguments->output_dir = arg;
        break;
    case 's':
        arguments->search = arg;
        break;
    case 'e':
        arguments->extract = arg;
        break;
    case 'p':
        arguments->print = true;
        break;
    case 'd':
        arguments->debug = true;
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = {options, parse_opt, args_doc, doc};

int main(int argc, char **argv) {
    struct arguments *args = calloc(sizeof(struct arguments), 1);
    int ret = 0;
    bool wopt = false;
    argp_parse(&argp, argc, argv, 0, 0, args);

    if (args->wows_base_dir != NULL) {
        ret = wows_get_latest_idx_dir(args->wows_base_dir, &args->input_dir);
        char *err_msg = wows_error_string(ret, NULL);
        wopt = true;
        if (ret != 0) {
            fprintf(stderr, "error: %s\n", err_msg);
            free(err_msg);
            free(args);
            return ret;
        }
        free(err_msg);
    }

    if (args->input == NULL && args->input_dir == NULL) {
        fprintf(stderr, "error: no -i <input file> or -I <input dir> arg or -W <wows dir> specified\n");
        return EXIT_FAILURE;
    }

    if ((args->output != NULL || args->output_dir != NULL) && args->extract == NULL) {
        fprintf(stderr, "error: please specify the file/dir to extract (-e '<FILE|DIR>')\n");
        return EXIT_FAILURE;
    }

    if ((args->output == NULL && args->output_dir == NULL) && args->extract != NULL) {
        fprintf(stderr, "error: please specify where to extract (-o 'FILE' or -O 'DIR')\n");
        return EXIT_FAILURE;
    }

    // Parsing the content
    WOWS_CONTEXT *context;
    if (args->debug) {
        context = wows_init_context(WOWS_DEBUG_RAW_RECORD | WOWS_DEBUG_FILE_LISTING);
    } else {
        context = wows_init_context(WOWS_NO_DEBUG);
    }

    if (args->input != NULL) {
        ret = wows_parse_index_file(args->input, context);
    }
    if (args->input_dir != NULL) {
        ret = wows_parse_index_dir(args->input_dir, context);
    }
    if (ret != 0) {
        char *err_msg = wows_error_string(ret, context);
        fprintf(stderr, "error: %s\n", err_msg);
        if (wopt) {
            free(args->input_dir);
        };
        free(err_msg);
        free(args);
        wows_free_context(context);
        return ret;
    }

    if (args->print) {
        wows_print_flat(context);
    }

    if (args->search != NULL) {
        int resc;
        char **res_files;
        ret = wows_search(context, args->search, WOWS_SEARCH_FILE_ONLY, &resc, &res_files);
        printf("Found %d matching files:\n", resc);
        for (int i = 0; i < resc; i++) {
            printf("%s\n", res_files[i]);
            free(res_files[i]);
        }
        free(res_files);
        if (ret != 0) {
            char *err_msg = wows_error_string(ret, context);
            fprintf(stderr, "error: %s\n", err_msg);
            if (wopt) {
                free(args->input_dir);
            };
            free(err_msg);
            free(args);
            wows_free_context(context);
            return ret;
        }
    }

    if (args->output != NULL && args->extract != NULL) {
        ret = wows_extract_file(context, args->extract, args->output);
        if (ret != 0) {
            char *err_msg = wows_error_string(ret, context);
            fprintf(stderr, "error: %s\n", err_msg);
            if (wopt) {
                free(args->input_dir);
            };
            free(err_msg);
            free(args);
            wows_free_context(context);
            return ret;
        }
    }

    if (args->output_dir != NULL && args->extract != NULL) {
        ret = wows_extract_dir(context, args->extract, args->output_dir);
        if (ret != 0) {
            char *err_msg = wows_error_string(ret, context);
            fprintf(stderr, "error: %s\n", err_msg);
            free(err_msg);
            free(args);
            wows_free_context(context);
            return ret;
        }
    }

    if (wopt) {
        free(args->input_dir);
    };
    free(args);
    wows_free_context(context);
    return 0;
}
