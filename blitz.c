/*
  +----------------------------------------------------------------------+
  | Authors: Alexey Rybak <alexey.rybak@gmail.com>,                      |
  |          downloads and online documentation:                         |
  |              - http://alexeyrybak.com/blitz/                         |
  |              - http://sourceforge.net/projects/blitz-templates/      |
  |                                                                      |
  |          Template analyzing is partially based on php_templates code |
  |          by Maxim Poltarak (http://php-templates.sf.net)             |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifndef PHP_WIN32
#include <sys/mman.h>
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "safe_mode.h"
#include "php_globals.h"
#include "php_ini.h"
#include "ext/standard/php_standard.h"
#include "ext/standard/info.h"
#include "ext/standard/html.h"

#ifdef PHP_WIN32
#include "win32/time.h"
#else
#include <sys/time.h>
#endif

#if PHP_API_VERSION >= 20041225 
#include "ext/date/php_date.h"
#endif

#include "php_blitz.h"

#define BLITZ_DEBUG 0 

ZEND_DECLARE_MODULE_GLOBALS(blitz)

// some declarations 
int blitz_exec_template(blitz_tpl *tpl, zval *id, unsigned char **result, unsigned long *result_len TSRMLS_DC);
int blitz_exec_nodes(blitz_tpl *tpl, tpl_node_struct *nodes, unsigned int n_nodes, zval *id,
    unsigned char **result, unsigned long *result_len, unsigned long *result_alloc_len,
    unsigned long parent_begin, unsigned long parent_end, zval *parent_ctx_data TSRMLS_DC);
inline int blitz_analyse (blitz_tpl *tpl TSRMLS_DC);

/* True global resources - no need for thread safety here */
static int le_blitz;

/* internal classes: Blitz, BlitzPack */
static zend_class_entry blitz_class_entry;

/* {{{ blitz_functions[] : Blitz class */
function_entry blitz_functions[] = {
    PHP_FALIAS(blitz,               blitz_init,                 NULL)
    PHP_FALIAS(load,                blitz_load,                 NULL)
    PHP_FALIAS(dump_struct,         blitz_dump_struct,          NULL)
    PHP_FALIAS(get_struct,          blitz_get_struct,           NULL)
    PHP_FALIAS(dump_iterations,     blitz_dump_iterations,      NULL)
    PHP_FALIAS(get_iterations,      blitz_get_iterations,       NULL)
    PHP_FALIAS(get_context,         blitz_get_context,          NULL)
    PHP_FALIAS(has_context,         blitz_has_context,          NULL)
    PHP_FALIAS(set_global,          blitz_set_global,           NULL)
    PHP_FALIAS(set_globals,         blitz_set_global,           NULL)
    PHP_FALIAS(get_globals,         blitz_get_globals,          NULL)
    PHP_FALIAS(set,                 blitz_set,                  NULL)
    PHP_FALIAS(parse,               blitz_parse,                NULL)
    PHP_FALIAS(include,             blitz_include,              NULL)
    PHP_FALIAS(iterate,             blitz_iterate,              NULL)
    PHP_FALIAS(context,             blitz_context,              NULL)
    PHP_FALIAS(block,               blitz_block,                NULL)
    PHP_FALIAS(fetch,               blitz_fetch,                NULL)
    PHP_FALIAS(clean,               blitz_clean,                NULL)
    PHP_FALIAS(dumpstruct,          blitz_dump_struct,          NULL)
    PHP_FALIAS(getstruct,           blitz_get_struct,           NULL)
    PHP_FALIAS(dumpiterations,      blitz_dump_iterations,      NULL)
    PHP_FALIAS(getiterations,       blitz_get_iterations,       NULL)
    PHP_FALIAS(hascontext,          blitz_has_context,          NULL)
    PHP_FALIAS(getcontext,          blitz_get_context,          NULL)
    PHP_FALIAS(setglobal,           blitz_set_global,           NULL)
    PHP_FALIAS(setglobals,          blitz_set_global,           NULL)
    PHP_FALIAS(getglobals,          blitz_get_globals,          NULL)
    {NULL, NULL, NULL}
};
/* }}} */

/* {{{ blitz_module_entry */
zend_module_entry blitz_module_entry = {
    STANDARD_MODULE_HEADER,
    "blitz",
    NULL,
    PHP_MINIT(blitz),
    PHP_MSHUTDOWN(blitz),
    PHP_RINIT(blitz),        /* Replace with NULL if there's nothing to do at request start */
    PHP_RSHUTDOWN(blitz),    /* Replace with NULL if there's nothing to do at request end */
    PHP_MINFO(blitz),
    NO_VERSION_YET,
    STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_BLITZ
ZEND_GET_MODULE(blitz)
#endif

/* {{{ PHP_INI */
ZEND_INI_MH(OnUpdateVarPrefixHanler) // i prefer to have OnUpdateChar, but there's no such handler
{
    char *p;
#ifndef ZTS
    char *base = (char *) mh_arg2;
#else
    char *base;
    base = (char *) ts_resource(*((int *) mh_arg2));
#endif

    p = (char *) (base+(size_t) mh_arg1);

    if (!new_value || new_value_length!=1) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "unable to set blitz.var_prefix (one character is allowed, like $ or %)");
        return FAILURE;
    }

    *p = new_value[0];
    return SUCCESS;
}

PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("blitz.var_prefix", BLITZ_TAG_VAR_PREFIX_S, PHP_INI_ALL,
        OnUpdateVarPrefixHanler, var_prefix, zend_blitz_globals, blitz_globals)
    STD_PHP_INI_ENTRY("blitz.tag_open", BLITZ_TAG_OPEN_DEFAULT, PHP_INI_ALL, 
        OnUpdateString, node_open, zend_blitz_globals, blitz_globals)
    STD_PHP_INI_ENTRY("blitz.tag_close", BLITZ_TAG_CLOSE_DEFAULT, PHP_INI_ALL, 
        OnUpdateString, node_close, zend_blitz_globals, blitz_globals)
    STD_PHP_INI_ENTRY("blitz.phpt_ctx_left", BLITZ_TAG_PHPT_CTX_LEFT, PHP_INI_ALL,
        OnUpdateString, phpt_ctx_left, zend_blitz_globals, blitz_globals)
    STD_PHP_INI_ENTRY("blitz.phpt_ctx_right", BLITZ_TAG_PHPT_CTX_RIGHT, PHP_INI_ALL,
        OnUpdateString, phpt_ctx_right, zend_blitz_globals, blitz_globals)
    STD_PHP_INI_ENTRY("blitz.path", "", PHP_INI_ALL,
        OnUpdateString, path, zend_blitz_globals, blitz_globals)
PHP_INI_END()
/* }}} */

/*  {{{ blitz_read_remplate_with_stream */
/**********************************************************************************************************************/
inline int blitz_read_with_stream(blitz_tpl *tpl, char *filename TSRMLS_DC) {
/**********************************************************************************************************************/
    php_stream *stream = NULL;

    if (php_check_open_basedir(filename TSRMLS_CC)) {
        return 0;
    }
    stream = php_stream_open_wrapper(filename, "rb",
        IGNORE_PATH|IGNORE_URL|IGNORE_URL_WIN|ENFORCE_SAFE_MODE|REPORT_ERRORS, NULL
    );
    if (!stream) {
        return 0;
    }
    tpl->static_data.body_len = php_stream_copy_to_mem(stream, &tpl->static_data.body, PHP_STREAM_COPY_ALL, 0);
    php_stream_close(stream);

    return 1;
}
/* }}} */

/*  {{{ blitz_read_remplate_with_fread */
/**********************************************************************************************************************/
inline int blitz_read_with_fread(blitz_tpl *tpl, char *filename TSRMLS_DC) {
/**********************************************************************************************************************/
    FILE *stream = NULL;
    unsigned int get_len = 0;

    if (php_check_open_basedir(filename TSRMLS_CC)) { 
        return 0; 
    }

    if (!(stream = fopen(filename, "rb"))) { 
        php_error_docref(NULL TSRMLS_CC, E_ERROR, 
            "unable to open file %s", filename 
        ); 
        return 0;
    }

    tpl->static_data.body = (char*)emalloc(BLITZ_INPUT_BUF_SIZE);
    tpl->static_data.body_len = 0; 
    while ((get_len = fread(tpl->static_data.body+tpl->static_data.body_len, 1, BLITZ_INPUT_BUF_SIZE, stream)) > 0) { 
        tpl->static_data.body_len += get_len; 
        tpl->static_data.body = (char*)erealloc(tpl->static_data.body, tpl->static_data.body_len + BLITZ_INPUT_BUF_SIZE); 
    } 
    fclose(stream); 

    return 1;
}
/* }}} */

/*  {{{ blitz_read_remplate_with_mmap */
#if HAVE_MMAP
/**********************************************************************************************************************/
inline int blitz_read_with_mmap(blitz_tpl *tpl, char *filename TSRMLS_DC) {
/**********************************************************************************************************************/
    int fd = 0;
    struct stat stat_info;
    void *srcfile = NULL;

    if (php_check_open_basedir(filename TSRMLS_CC)){
        return 0;
    }

    if (-1 == (fd = open(filename, 0))) {
        php_error_docref(NULL TSRMLS_CC, E_ERROR,
            "unable to open file %s", filename
        );
        return 0;
    }

   if (-1 == fstat(fd, &stat_info)) {
        php_error_docref(NULL TSRMLS_CC, E_ERROR,
            "unable to open file %s", filename
        );

        return 0;
    }

    srcfile = mmap(NULL, stat_info.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (srcfile == (void*)MAP_FAILED) {
        return 0;
    }

    tpl->static_data.body = (char*)emalloc(stat_info.st_size+1);
    tpl->static_data.body_len = stat_info.st_size;
    memcpy(tpl->static_data.body, srcfile, stat_info.st_size);
    tpl->static_data.body[stat_info.st_size] = '\0';

    close(fd);
    munmap(srcfile, stat_info.st_size);

    return 1;
}
#endif
/* }}} */

/*  {{{ blitz_init_tpl_base */
/**********************************************************************************************************************/
blitz_tpl *blitz_init_tpl_base(HashTable *globals, zval *iterations TSRMLS_DC){
/**********************************************************************************************************************/
    zval *empty_array = NULL;

    blitz_tpl *tpl = (blitz_tpl*)emalloc(sizeof(blitz_tpl));
    if (!tpl) {
        php_error_docref(
            NULL TSRMLS_CC, E_ERROR,
            "INTERNAL ERROR: unable to allocate memory for blitz template structure"
        );
        return NULL;
    }

    tpl->static_data.name = NULL;
    tpl->static_data.body = NULL;

    tpl->flags = 0;
    tpl->static_data.n_nodes = 0;
    tpl->static_data.config = (tpl_config_struct*)emalloc(sizeof(tpl_config_struct));
    if (!tpl->static_data.config) {
        php_error_docref(
            NULL TSRMLS_CC, E_ERROR,
            "INTERNAL ERROR: unable to allocate memory for blitz template config structure"
        );
        return NULL;
    }

    tpl->static_data.config->open_node = BLITZ_G(node_open);
    tpl->static_data.config->close_node = BLITZ_G(node_close);
    tpl->static_data.config->var_prefix = (char)BLITZ_G(var_prefix);
    tpl->static_data.config->l_open_node = strlen(tpl->static_data.config->open_node);
    tpl->static_data.config->l_close_node = strlen(tpl->static_data.config->close_node);
    tpl->static_data.config->phpt_ctx_left = BLITZ_G(phpt_ctx_left);
    tpl->static_data.config->phpt_ctx_right = BLITZ_G(phpt_ctx_right);
    tpl->static_data.config->l_phpt_ctx_left = strlen(tpl->static_data.config->phpt_ctx_left);
    tpl->static_data.config->l_phpt_ctx_right = strlen(tpl->static_data.config->phpt_ctx_right);

    tpl->static_data.nodes = NULL;
    tpl->loop_stack_level = 0;

    if (iterations) {
        /* 
           "Inherit" iterations from opener, used for incudes. Just make a copy mark template 
           with special flag BLITZ_FLAG_ITERATION_IS_OTHER not to free this object.
        */
        tpl->iterations = iterations;
        tpl->flags |= BLITZ_FLAG_ITERATION_IS_OTHER;
    } else {
        MAKE_STD_ZVAL(tpl->iterations);
        array_init(tpl->iterations);
    }

    tpl->current_iteration = NULL;
    tpl->last_iteration = NULL;
    tpl->current_iteration_parent = & tpl->iterations;
    tpl->current_path = "/";

    tpl->tmp_buf = emalloc(BLITZ_TMP_BUF_MAX_LEN);
    tpl->static_data.fetch_index = NULL;

    // allocate "personal" hash-table
    if (globals == NULL) { 
        tpl->hash_globals = NULL;
        ALLOC_HASHTABLE(tpl->hash_globals);

        if (
            !tpl->hash_globals 
            || (FAILURE == zend_hash_init(tpl->hash_globals, 8, NULL, ZVAL_PTR_DTOR, 0))
        ) {
            php_error_docref(
                NULL TSRMLS_CC, E_ERROR,
                "INTERNAL ERROR: unable to allocate or fill memory for blitz params"
            );
            return NULL;
        }
    } else {
        /* 
           "Inherit" globals from opener, used for includes. Just make a copy mark template
           with special flag BLITZ_FLAG_GLOBALS_IS_OTHER not to free this object.
        */
        tpl->hash_globals = globals;
        tpl->flags |= BLITZ_FLAG_GLOBALS_IS_OTHER;
    }

    tpl->ht_tpl_name = NULL;
    ALLOC_HASHTABLE(tpl->ht_tpl_name);
    if (
        !tpl->ht_tpl_name
        || (FAILURE == zend_hash_init(tpl->ht_tpl_name, 8, NULL, ZVAL_PTR_DTOR, 0))
    ) {
        php_error_docref(
            NULL TSRMLS_CC, E_ERROR,
            "INTERNAL ERROR: unable to allocate or fill memory for blitz template index"
        );
        return NULL;
    }

    tpl->itpl_list = 
        (blitz_tpl**)emalloc(BLITZ_ITPL_ALLOC_INIT*sizeof(blitz_tpl*));
    if (!tpl->itpl_list) {
        php_error_docref(
            NULL TSRMLS_CC, E_ERROR,
            "INTERNAL ERROR: unable to allocate memory for inner-include blitz template list"
        );
        return NULL;
    }
    tpl->itpl_list_len = 0;
    tpl->itpl_list_alloc = BLITZ_ITPL_ALLOC_INIT;

    return tpl;
}
/* }}} */

/*  {{{ blitz_init_tpl */
/**********************************************************************************************************************/
blitz_tpl *blitz_init_tpl(
/**********************************************************************************************************************/
    char *filename, 
    HashTable *globals,
    zval *iterations TSRMLS_DC) {
/**********************************************************************************************************************/
    FILE *f = NULL;
    char *global_path = NULL;
    int global_path_len = 0;
    char normalized_buf[BLITZ_FILE_PATH_MAX_LEN];
    char *filename_normalized = filename;
#ifdef BLITZ_USE_STREAMS
    php_stream *stream = NULL;
#endif
    unsigned int filename_len = 0, filename_normalized_len = 0;
    unsigned int add_buffer_len = 0;
    int result = 0;

    blitz_tpl *tpl = blitz_init_tpl_base(globals, iterations TSRMLS_CC);

    if (!tpl) return NULL;

    if (!filename) return tpl; // OK, will be loaded after

    filename_normalized_len = filename_len = strlen(filename);

    if ('/' != filename[0]) {
        global_path = BLITZ_G(path);
        global_path_len = strlen(global_path);
        if (global_path_len) {
            if (global_path_len + filename_len > BLITZ_FILE_PATH_MAX_LEN) {
                php_error_docref(NULL TSRMLS_CC, E_ERROR,
                    "INTERNAL ERROR: file path is too long, increase BLITZ_MAX_FILE_PATH");
                return NULL;
            }
            filename_normalized = normalized_buf;
            memcpy(filename_normalized, global_path, global_path_len);
            memcpy(filename_normalized + global_path_len, filename, filename_len);
            filename_normalized_len = filename_len + global_path_len;
            filename_normalized[filename_normalized_len] = '\x0';
        }
    } 

/* 
It seems to be 10% faster to use just fread or mmap than streams on lebowski-bench. 
However under win32 there are errors with relative paths and fread, 
I didn't checked why yet ;) See php_blitz.h for BLITZ_USE_STREAMS definition.
*/

#ifdef BLITZ_USE_STREAMS
    result = blitz_read_with_stream(tpl, filename_normalized TSRMLS_CC);
#else
#ifdef BLITZ_USE_MMAP
    result = blitz_read_with_mmap(tpl, filename_normalized TSRMLS_CC);
#else
    result = blitz_read_with_fread(tpl, filename_normalized TSRMLS_CC);
#endif
#endif

    if (0 == result) return tpl;

    // search algorithm requires lager buffer: body_len + add_buffer
    add_buffer_len = MAX(
        MAX(tpl->static_data.config->l_open_node,tpl->static_data.config->l_close_node), 
        MAX(tpl->static_data.config->l_phpt_ctx_left,tpl->static_data.config->l_phpt_ctx_right) 
    );

    tpl->static_data.body = erealloc(tpl->static_data.body,tpl->static_data.body_len + add_buffer_len);
    memset(tpl->static_data.body + tpl->static_data.body_len,'\x0',add_buffer_len);

    tpl->static_data.name = emalloc(filename_normalized_len+1);
    if (tpl->static_data.name){
        memcpy(tpl->static_data.name, filename_normalized, filename_normalized_len);
        tpl->static_data.name[filename_normalized_len] = '\x0';
    }

    return tpl;
}
/* }}} */

/*  {{{ blitz_load_body */
/**********************************************************************************************************************/
int blitz_load_body(
/**********************************************************************************************************************/
    blitz_tpl *tpl,
    zval *body TSRMLS_DC) {
/**********************************************************************************************************************/
    unsigned int add_buffer_len = 0;
    char *name = "noname_loaded_from_zval";
    int name_len = strlen(name);

    if (!tpl || !body) {
        return 0;
    }

    tpl->static_data.body_len = Z_STRLEN_P(body);
    if (tpl->static_data.body_len) {
        add_buffer_len = MAX(
            MAX(tpl->static_data.config->l_open_node,tpl->static_data.config->l_close_node),
            MAX(tpl->static_data.config->l_phpt_ctx_left,tpl->static_data.config->l_phpt_ctx_right)
        );

        tpl->static_data.body = emalloc(tpl->static_data.body_len + add_buffer_len);
        memcpy(tpl->static_data.body,Z_STRVAL_P(body),Z_STRLEN_P(body));
        memset(tpl->static_data.body + tpl->static_data.body_len,'\x0',add_buffer_len);
    }

    tpl->static_data.name = emalloc(name_len+1);
    if (tpl->static_data.name){
        memcpy(tpl->static_data.name,name,name_len);
        tpl->static_data.name[name_len] = '\x0';
    }

    return 1;
}
/* }}} */

/*  {{{ blitz_free_tpl(blitz_tpl *tpl) */
/**********************************************************************************************************************/
void blitz_free_tpl(blitz_tpl *tpl) {
/**********************************************************************************************************************/
    unsigned char n_nodes=0, n_args=0, i=0, j=0;

    if (!tpl) return;

    if (tpl->static_data.config) 
        efree(tpl->static_data.config);

    n_nodes = tpl->static_data.n_nodes;
    if (n_nodes) {
        for(i=0;i<n_nodes;++i) {
            n_args = tpl->static_data.nodes[i].n_args;
            for(j=0;j<n_args;++j) {
                if (tpl->static_data.nodes[i].args[j].name) {
                    efree(tpl->static_data.nodes[i].args[j].name); 
                }
            }
            if (tpl->static_data.nodes[i].lexem)
                efree(tpl->static_data.nodes[i].lexem);
            if (tpl->static_data.nodes[i].args)
                efree(tpl->static_data.nodes[i].args);
            if (tpl->static_data.nodes[i].children)
                efree(tpl->static_data.nodes[i].children);
        }
    }

    if (tpl->static_data.name)
        efree(tpl->static_data.name);
    if (tpl->static_data.nodes)
        efree(tpl->static_data.nodes);

    if (tpl->static_data.body) 
        efree(tpl->static_data.body); 

    if (tpl->hash_globals && !(tpl->flags & BLITZ_FLAG_GLOBALS_IS_OTHER))
        FREE_HASHTABLE(tpl->hash_globals);

    if (tpl->ht_tpl_name)
        FREE_HASHTABLE(tpl->ht_tpl_name);

    if (tpl->static_data.fetch_index)
        FREE_HASHTABLE(tpl->static_data.fetch_index);

    if (tpl->tmp_buf)
        efree(tpl->tmp_buf);

    if (tpl->iterations && !(tpl->flags & BLITZ_FLAG_ITERATION_IS_OTHER))
        FREE_ZVAL(tpl->iterations);

    if (tpl->itpl_list) { 
        for(i=0; i<tpl->itpl_list_len; i++) {
            if (tpl->itpl_list[i])
                blitz_free_tpl(tpl->itpl_list[i]);
        }
        efree(tpl->itpl_list);
    }

    efree(tpl);
}
/* }}} */

/* {{{ blitz_include_tpl_cached */
/**********************************************************************************************************************/
int blitz_include_tpl_cached(
    blitz_tpl *tpl,
    char *filename, 
    unsigned int filename_len, 
    zval *iteration_params,
    blitz_tpl **itpl TSRMLS_DC){
/**********************************************************************************************************************/
    zval **desc = NULL;
    unsigned long itpl_idx = 0;
    zval *temp = NULL;

    // try to find already parsed tpl index
    if (SUCCESS == zend_hash_find(tpl->ht_tpl_name, filename, filename_len, (void **)&desc)) {
        *itpl = tpl->itpl_list[Z_LVAL_PP(desc)];
        if (iteration_params) {
            (*itpl)->iterations = iteration_params;
            (*itpl)->flags |= BLITZ_FLAG_ITERATION_IS_OTHER;
        } else {
            if ((*itpl)->iterations && !((*itpl)->flags & BLITZ_FLAG_ITERATION_IS_OTHER)) {
                zend_hash_clean(Z_ARRVAL_P((*itpl)->iterations));
            } else {
                MAKE_STD_ZVAL((*itpl)->iterations);
                array_init((*itpl)->iterations);
            }
            (*itpl)->flags ^= BLITZ_FLAG_ITERATION_IS_OTHER;
        }
        return 1;
    } 

    // initialize template
    if (!(*itpl = blitz_init_tpl(filename, tpl->hash_globals, iteration_params TSRMLS_CC))) {
        blitz_free_tpl(*itpl);
        return 0;
    }

    // analyse template
    if (!blitz_analyse(*itpl TSRMLS_CC)) {
        blitz_free_tpl(*itpl);
        return 0;
    }

    // realloc list if needed
    if (tpl->itpl_list_len >= tpl->itpl_list_alloc-1) {
        tpl->itpl_list = (blitz_tpl**)erealloc(
            tpl->itpl_list,(tpl->itpl_list_alloc<<1)*sizeof(blitz_tpl*)
        );
        if (tpl->itpl_list) {
            tpl->itpl_list_alloc <<= 1;
        } else {
            php_error_docref(NULL TSRMLS_CC, E_WARNING,
                "INTERNAL ERROR: cannot realloc memory for inner-template list"
            );
            blitz_free_tpl(*itpl);
            return 0;
        }
    }

    // save template index values
    itpl_idx = tpl->itpl_list_len;
    tpl->itpl_list[itpl_idx] = *itpl;
    MAKE_STD_ZVAL(temp);
    ZVAL_LONG(temp, itpl_idx);
    zend_hash_update(tpl->ht_tpl_name, filename, filename_len, &temp, sizeof(zval *), NULL);
    tpl->itpl_list_len++;

    return 1;
}
/* }}} */

/* {{{ blitz_resource_dtor (zend_rsrc_list_entry *rsrc TSRMLS_DC) */
/**********************************************************************************************************************/
static void blitz_resource_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC) {
/**********************************************************************************************************************/
    blitz_tpl *tpl = NULL;
    tpl = (blitz_tpl*)rsrc->ptr;
    blitz_free_tpl(tpl);
}
/* }}} */

/* {{{ php_blitz_init_globals */
/**********************************************************************************************************************/
static void php_blitz_init_globals(zend_blitz_globals *blitz_globals)
/**********************************************************************************************************************/
{
    blitz_globals->var_prefix = BLITZ_TAG_VAR_PREFIX;
    blitz_globals->node_open = BLITZ_TAG_OPEN_DEFAULT;
    blitz_globals->node_close = BLITZ_TAG_CLOSE_DEFAULT;

}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
/**********************************************************************************************************************/
PHP_MINIT_FUNCTION(blitz)
/**********************************************************************************************************************/
{
    ZEND_INIT_MODULE_GLOBALS(blitz, php_blitz_init_globals, NULL);
    REGISTER_INI_ENTRIES();
    le_blitz = zend_register_list_destructors_ex(
        blitz_resource_dtor, NULL, "blitz template", module_number);

    INIT_CLASS_ENTRY(blitz_class_entry, "blitz", blitz_functions);
    zend_register_internal_class(&blitz_class_entry TSRMLS_CC);

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION */
/**********************************************************************************************************************/
PHP_MSHUTDOWN_FUNCTION(blitz)
/**********************************************************************************************************************/
{
    UNREGISTER_INI_ENTRIES();
    return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION */
/**********************************************************************************************************************/
PHP_RINIT_FUNCTION(blitz)
/**********************************************************************************************************************/
{
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_RSHUTDOWN_FUNCTION */
/**********************************************************************************************************************/
PHP_RSHUTDOWN_FUNCTION(blitz)
/**********************************************************************************************************************/
{
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION */
/**********************************************************************************************************************/
PHP_MINFO_FUNCTION(blitz)
/**********************************************************************************************************************/
{
    char *buf[2048];
    char *node_open =  BLITZ_G(node_open);
    char *node_close = BLITZ_G(node_close);
    char var_prefix = (char)BLITZ_G(var_prefix);
    char *phpt_l = BLITZ_G(phpt_ctx_left);
    char *phpt_r = BLITZ_G(phpt_ctx_right);

    php_info_print_table_start();
    php_info_print_table_row(2, "Blitz support", "enabled");
    php_info_print_table_row(2, "Version", "0.5.7-develko");
    php_info_print_table_end();

    DISPLAY_INI_ENTRIES();

    php_info_print_table_start();
    php_info_print_table_colspan_header(2, "Mini template HOWTO ;)");
    snprintf((char*)buf, 2048, "%s %cvar; %s or %s %cvar %s", node_open, var_prefix, node_close, node_open, var_prefix, node_close);
    php_info_print_table_row(2, "Varible:", buf);
    snprintf((char*)buf, 2048, "%s %cvar; %s or %s %cvar %s", phpt_l, var_prefix, phpt_r, phpt_l, var_prefix, phpt_r);
    php_info_print_table_row(2, "Varible (alt):", buf);
    snprintf((char*)buf, 2048, "%s test(); %s or %s test('a',\"b\",%cc,TRUE,0) %s", node_open, node_close, node_open, var_prefix, node_close);
    php_info_print_table_row(2, "Method:", buf);
    snprintf((char*)buf, 2048, "%stest();%s or %stest('a',\"b\",%cc,TRUE,0)%s", phpt_l, phpt_r, phpt_l, var_prefix, phpt_r);
    php_info_print_table_row(2, "Method (alt):", buf);
    snprintf((char*)buf, 2048, "%s BEGIN something %s something %s END %s ", node_open, node_close, node_open, node_close);
    php_info_print_table_row(2, "Context:", buf);
    snprintf((char*)buf, 2048, "%sBEGIN something%s something %sEND%s ", phpt_l, phpt_r, phpt_l, phpt_r);
    php_info_print_table_row(2, "Context (alt):", buf);

    php_info_print_table_end();

}
/* }}} */

// debug functions
/* {{{ php_blitz_dump_struct_plain(blitz_tpl *tpl) */
/**********************************************************************************************************************/
void php_blitz_dump_struct_plain(blitz_tpl *tpl) {
/**********************************************************************************************************************/
    unsigned long i = 0, j = 0;
    tpl_node_struct *node = NULL;

    php_printf("== PLAIN STRUCT (%ld nodes):",(unsigned long)tpl->static_data.n_nodes);
    for (i=0; i<tpl->static_data.n_nodes; ++i) {
        node = & tpl->static_data.nodes[i];
        php_printf("\n%s[%d] (%ld(%ld), %ld(%ld)); ",
            node->lexem,
            node->type,
            node->pos_begin, node->pos_begin_shift,
            node->pos_end, node->pos_end_shift
        );
        if (BLITZ_IS_METHOD(node->type)) {
            php_printf("ARGS(%d): ",node->n_args);
            for (j=0;j<node->n_args;++j) {
                if (j) php_printf(",");
                php_printf("%s(%d)",node->args[j].name,node->args[j].type);
            }
            if (node->children) {
                php_printf("; CHILDREN(%d):",node->n_children);
            }
        }
    }
    return;
}
/* }}} */

/* {{{ php_blitz_dump_node(tpl_node_struct *node, unsigned int *p_level) */
/**********************************************************************************************************************/
void php_blitz_dump_node(tpl_node_struct *node, unsigned int *p_level) {
/**********************************************************************************************************************/
    unsigned long j = 0;
    unsigned int level = 0;
    char shift_str[] = "--------------------------------"; 
    if (!node) return;
    level = p_level ? *p_level : 0;
    if (level>=10) level = 10;
    memset(shift_str,' ',2*level+1);
    shift_str[2*level+1] = '^';
    shift_str[2*level+3] = '\x0';
    php_printf("\n%s%s[%u] (%lu(%lu), %lu(%lu)); ",
        shift_str,
        node->lexem,
        (unsigned int)node->type,
        node->pos_begin, node->pos_begin_shift,
        node->pos_end, node->pos_end_shift
    );
    if (BLITZ_IS_METHOD(node->type)) {
        php_printf("ARGS(%d): ",node->n_args);
        for (j=0;j<node->n_args;++j) {
            if (j) php_printf(",");
            php_printf("%s(%d)",node->args[j].name,node->args[j].type);
        }
        if (node->children) {
            php_printf("; CHILDREN(%d):",node->n_children);
            for (j=0;j<node->n_children;++j) {
                (*p_level)++;
                php_blitz_dump_node(node->children[j],p_level);
                (*p_level)--;
            }
        }
    }
}
/* }}} */

/* {{{ php_blitz_dump_struct(blitz_tpl *tpl) */
/**********************************************************************************************************************/
void php_blitz_dump_struct(blitz_tpl *tpl) {
/**********************************************************************************************************************/
    unsigned long i = 0;
    unsigned int level = 0; 
    unsigned int last_close = 0;

    php_printf("== TREE STRUCT (%ld nodes):",(unsigned long)tpl->static_data.n_nodes);
    for (i=0; i<tpl->static_data.n_nodes; ++i) {
        if (tpl->static_data.nodes[i].pos_begin>=last_close) {
            php_blitz_dump_node(tpl->static_data.nodes+i, &level);
            last_close = tpl->static_data.nodes[i].pos_end;
        }
    }

    php_printf("\n");
    php_blitz_dump_struct_plain(tpl);
    php_printf("\n");

    return;
}
/* }}} */

/* {{{ php_blitz_get_node_paths(zval *list, tpl_node_struct *node, char *parent_path) */
/**********************************************************************************************************************/
void php_blitz_get_node_paths(zval *list, tpl_node_struct *node, char *parent_path) {
/**********************************************************************************************************************/
    unsigned long j = 0;
    char suffix[2] = "\x0";
    char path[BLITZ_CONTEXT_PATH_MAX_LEN] = "\x0";

    if (!node) return;
    if (node->type == BLITZ_NODE_TYPE_BEGIN) { // non-finalized node (end was not found, error)
        return;
    }

    if (node->type == BLITZ_NODE_TYPE_CONTEXT) {
        suffix[0] = '/';
        // contexts: use context name from args instead of useless "BEGIN"
        sprintf(path, "%s%s%s", parent_path, node->args[0].name, suffix); 
    } else {
        sprintf(path, "%s%s%s", parent_path, node->lexem, suffix);
    }
    add_next_index_string(list, path, 1);

    if (node->type == BLITZ_NODE_TYPE_CONTEXT) {
        for (j=0;j<node->n_children;++j) {
            php_blitz_get_node_paths(list, node->children[j], path);
        }
    }
}
/* }}} */

/* {{{ php_blitz_get_path_list(blitz_tpl *tpl, zval *list) */
/**********************************************************************************************************************/
void php_blitz_get_path_list(blitz_tpl *tpl, zval *list) {
/**********************************************************************************************************************/
    unsigned long i = 0;
    unsigned int level = 0;
    unsigned int last_close = 0;
    char path[BLITZ_CONTEXT_PATH_MAX_LEN] = "/";

    for (i=0; i<tpl->static_data.n_nodes; ++i) {
        if (tpl->static_data.nodes[i].pos_begin>=last_close) {
            php_blitz_get_node_paths(list, tpl->static_data.nodes+i, path);
            last_close = tpl->static_data.nodes[i].pos_end;
        }
    }

    return;
}
/* }}} */
# define REALLOC_POS_IF_EXCEEDS                                                 \
    if (p >= alloc_size) {                                                      \
        alloc_size = alloc_size << 1;                                           \
        *pos = erealloc(*pos,alloc_size*sizeof(tag_pos));                       \
    }                                                                           \

/* {{{ blitz_bm_search */
/**********************************************************************************************************************/
inline void blitz_bm_search (                                                        
/**********************************************************************************************************************/
    unsigned char *haystack, 
    unsigned long haystack_length,
    unsigned char *needle,
    unsigned int  needle_len,
    unsigned char pos_type,
    unsigned int  *n_found,
    tag_pos **pos,
    unsigned int  *pos_size,
    unsigned int  *pos_alloc_size TSRMLS_DC) {
/**********************************************************************************************************************/
    register unsigned long  i=0, j=0, k=0, shift=0, cmp=0, p=0, j_max=0, needle_len_m1 = needle_len - 1;
    register unsigned long  haystack_len=haystack_length;
    register unsigned long  alloc_size = *pos_alloc_size;
    unsigned long           bmBc[256];
    tag_pos *pos_ptr = NULL;

    if (haystack_len < (unsigned long)needle_len) {
        return;
    }

    for (i=0; i < 256; ++i) bmBc[i] = needle_len;
    for (i=0; i < needle_len - 1; ++i) bmBc[needle[i]] = needle_len_m1 - i;

    shift = bmBc[needle[needle_len_m1]];
    bmBc[needle[needle_len - 1]] = 0;
    memset(haystack + haystack_len, needle[needle_len_m1], needle_len);
    j = 0;
    i = 0;
    p = *pos_size;
    j_max = haystack_len - needle_len + 1;
    while (j < j_max) {
        k = bmBc[haystack[j + needle_len_m1]];
        while (k != 0) {
            j += k; k = bmBc[haystack[j + needle_len_m1]];
            j += k; k = bmBc[haystack[j + needle_len_m1]];
            j += k; k = bmBc[haystack[j + needle_len_m1]];
        }
        if (j < j_max) {
            for(cmp = 0; cmp<needle_len; ++cmp)
                if (needle[cmp] != haystack[j+cmp]) break;
            if (cmp == needle_len) {
                REALLOC_POS_IF_EXCEEDS;
                pos_ptr = (*pos+p);
                pos_ptr->pos = j;
                pos_ptr->type = pos_type;
                ++i; 
                ++p;
            }
        }
        j += shift;
    }

    *n_found = i;
    *pos_alloc_size = alloc_size;
    *pos_size = p;
}
/* }}} */

#define INIT_CALL_ARGS                                                          \
    node->args = (call_arg*)                                                    \
        emalloc(BLITZ_CALL_ALLOC_ARG_INIT*sizeof(call_arg));                    \
    node->n_args = 0;                                                           \
    n_arg_alloc = BLITZ_CALL_ALLOC_ARG_INIT;

# define REALLOC_ARG_IF_EXCEEDS                                                 \
    if (arg_id >= n_arg_alloc) {                                                \
        n_arg_alloc = n_arg_alloc << 1;                                         \
        node->args = (call_arg*)                                                \
            erealloc(node->args,n_arg_alloc*sizeof(call_arg));                  \
    }  

# define ADD_CALL_ARGS(token, i_len, i_type)                                    \
    REALLOC_ARG_IF_EXCEEDS;                                                     \
    i_arg = node->args + arg_id;                                                \
    i_arg->name = estrndup((token),(i_len));                                    \
    i_arg->len = (i_len);                                                       \
    i_arg->type = (i_type);                                                     \
    ++arg_id;                                                                   \
    node->n_args = arg_id;                                                     

/* {{{ blitz_parse_call */
/**********************************************************************************************************************/
inline void blitz_parse_call (
/**********************************************************************************************************************/
    unsigned char *text, 
    unsigned int len_text, 
    tpl_node_struct *node, 
    unsigned int *true_lexem_len,
    char var_prefix,
    unsigned char is_phpt_tag, 
    unsigned char *error TSRMLS_DC) {
/**********************************************************************************************************************/
    register unsigned char *c = text;
    unsigned char *p = NULL;
    register unsigned char symb = 0, i_symb = 0, is_path = 0;
    unsigned char state = BLITZ_CALL_STATE_ERROR;
    unsigned char ok = 0;
    register unsigned int pos = 0, i_pos = 0, i_len = 0;
    unsigned char was_escaped;
    unsigned char main_token[1024], token[1024];
    unsigned char n_arg_alloc = 0;
    register unsigned char i_type = 0;
    unsigned char arg_id = 0;
    call_arg *i_arg = NULL;
    char cl = 0;
    char *ptr_token = &cl;

    // init node
    node->n_args = 0;
    node->args = NULL;
    node->lexem = NULL;
    node->type = BLITZ_TYPE_METHOD;

    *true_lexem_len = 0; // used for parameters only

    BLITZ_SKIP_BLANK(c,i_pos,pos);

    if (BLITZ_DEBUG) {
        char *tmp = estrndup(text, len_text);
        tmp[len_text-1] = '\x0';
        php_printf("blitz_parse_call, started at pos=%ld, c=%c\n", pos, *c);
        php_printf("text: %s\n", tmp);
    }

    p = main_token;
    i_pos = 0;

    // parameter or method?
    if (*c == var_prefix) { // scan parameter
        i_pos=0; ++c; ++pos;
        BLITZ_SCAN_VAR(c,p,i_pos,i_symb,is_path);
        pos+=i_pos;
        if (i_pos!=0) {
            node->lexem = estrndup(main_token,i_pos);
            *true_lexem_len = i_pos;
            if (is_path) {
                node->type = BLITZ_NODE_TYPE_VAR_PATH;
            } else {
                node->type = BLITZ_NODE_TYPE_VAR;
            }
            state = BLITZ_CALL_STATE_FINISHED;
        }
    } else if (BLITZ_IS_ALPHA(*c)) { // scan function
        if (BLITZ_DEBUG) php_printf("D1: pos=%ld, i_pos=%ld, c=%c\n", pos, i_pos, *c);
        BLITZ_SCAN_ALNUM(c,p,i_pos,i_symb);
        if (BLITZ_DEBUG) php_printf("D2: pos=%ld, i_pos=%ld, c=%c\n", pos, i_pos, *c);
        pos+=i_pos-1;
        c = text + pos;
        if (BLITZ_DEBUG) php_printf("D3: pos=%ld, i_pos=%ld, c=%c\n", pos, i_pos, *c);

        if (i_pos>0) {
            node->lexem = estrndup(main_token,i_pos);
            node->type = BLITZ_TYPE_METHOD;
            *true_lexem_len = i_pos-1;
            ++pos; ++c;

            if (BLITZ_DEBUG) php_printf("METHOD: %s, pos=%ld, c=%c, type=%u\n", node->lexem, pos, *c, node->type);
            if (*c == '(') { // has arguments
                ok = 1; ++pos; 
                BLITZ_SKIP_BLANK(c,i_pos,pos);
                ++c;
                if (*c == ')') { // move to finished without any middle-state if no args
                    ++pos;
                    state = BLITZ_CALL_STATE_FINISHED;
                } else {
                    INIT_CALL_ARGS;
                    state = BLITZ_CALL_STATE_NEXT_ARG;

                    // predefined method?
                    if (0 == strcmp(main_token,BLITZ_NODE_TYPE_IF_S)) {
                        node->type = BLITZ_NODE_TYPE_IF;
                    } else if (0 == strcmp(main_token,BLITZ_NODE_TYPE_INCLUDE_S)) {
                        node->type = BLITZ_NODE_TYPE_INCLUDE;
                    } else if (0 == strcmp(main_token,BLITZ_NODE_TYPE_WRAPPER_ESCAPE_S)) {
                        node->type = BLITZ_NODE_TYPE_WRAPPER;
                        node->flags = BLITZ_NODE_TYPE_WRAPPER_ESCAPE;
                    } else if (0 == strcmp(main_token,BLITZ_NODE_TYPE_WRAPPER_DATE_S)) {
                        node->type = BLITZ_NODE_TYPE_WRAPPER;
                        node->flags = BLITZ_NODE_TYPE_WRAPPER_DATE;
                    } 
                }
            } else {
                ok = 1;
                if (BLITZ_TAG_IS_BEGIN(main_token)) {
                    INIT_CALL_ARGS; 
                    state = BLITZ_CALL_STATE_BEGIN;
                    node->type = BLITZ_NODE_TYPE_BEGIN;
                } else if (BLITZ_TAG_IS_END(main_token)) {
                    INIT_CALL_ARGS; 
                    state = BLITZ_CALL_STATE_END;
                    node->type = BLITZ_NODE_TYPE_END;
                } else {
                    // for the partial support of php_templates, functions without brackets are treated as parameters.
                    // see details in php_blitz.h
                    if (BLITZ_SUPPORT_PHPT_TAGS_PARTIALLY && (BLITZ_SUPPORT_PHPT_NOBRAKET_FUNCTIONS_ARE_VARS || is_phpt_tag)) {
                        node->type = BLITZ_NODE_TYPE_VAR;
                    } else { // case insensitivity for methods
                        zend_str_tolower(node->lexem,i_pos);
                    }
                    state = BLITZ_CALL_STATE_FINISHED;
                }
            }
        }

        if (BLITZ_DEBUG) php_printf("LOOP_BEGIN: pos=%ld, c=%c\n", pos, *c);
        c = text + pos;
        while ((symb = *c) && ok) {
            if (BLITZ_DEBUG) php_printf("LOOP_BODY: pos=%ld, state=%ld, c=%c\n", pos, state, symb);
            switch(state) {
                case BLITZ_CALL_STATE_BEGIN:
                    symb = *c;
                    BLITZ_SKIP_BLANK(c,i_pos,pos);
                    i_len = i_pos = ok = 0;
                    p = token;
                    BLITZ_SCAN_ALNUM(c,p,i_len,i_symb);

                    if (i_len!=0) {
                        ok = 1; 
                        pos += i_len;
                        ADD_CALL_ARGS(token, i_len, i_type);
                        state = BLITZ_CALL_STATE_FINISHED;
                    } else {
                        state = BLITZ_CALL_STATE_ERROR;
                    }
                    if (BLITZ_DEBUG) php_printf("STATE_BEGIN: pos=%ld, c=%c, next_state=%d\n", pos, *c, state);
                    break;
                case BLITZ_CALL_STATE_END:
                    i_pos = 0;
                    BLITZ_SKIP_BLANK(c,i_pos,pos); i_pos = 0; symb = *c;
                    if (BLITZ_IS_ALPHA(symb)) {
                        BLITZ_SCAN_ALNUM(c,p,i_pos,i_symb); pos += i_pos; i_pos = 0;
                    }
                    state = BLITZ_CALL_STATE_FINISHED;
                    if (BLITZ_DEBUG) php_printf("STATE_END: pos=%ld, c=%c, next_state = %d\n", pos, *c, state);
                    break;
                case BLITZ_CALL_STATE_NEXT_ARG:
                    BLITZ_SKIP_BLANK(c,i_pos,pos);
                    symb = *c;
                    i_len = i_pos = ok = 0;
                    p = token;
                    i_type = 0;

                    if (BLITZ_DEBUG) php_printf("STATE_NEXT_ARG: %ld, %ld; c = %c\n", pos, len_text, *c);
                    if (symb == var_prefix) {
                        ++c; ++pos;
                        //BLITZ_SCAN_ALNUM(c,p,i_pos,i_symb);
                        is_path = 0;
                        BLITZ_SCAN_VAR(c,p,i_pos,i_symb,is_path);
                        if (i_pos!=0) ok = 1;
                        i_type = is_path ? BLITZ_ARG_TYPE_VAR_PATH : BLITZ_ARG_TYPE_VAR;
                        i_len = i_pos;
                    } else if (symb == '\'') {
                        ++c; ++pos;
                        BLITZ_SCAN_SINGLE_QUOTED(c,p,i_pos,i_len,ok);
                        i_pos++;
                        i_type = BLITZ_ARG_TYPE_STR;
                    } else if (symb == '\"') {
                        ++c; ++pos;
                        BLITZ_SCAN_DOUBLE_QUOTED(c,p,i_pos,i_len,ok);
                        i_pos++;
                        i_type = BLITZ_ARG_TYPE_STR;
                    } else if (BLITZ_IS_NUMBER(symb)) {
                        BLITZ_SCAN_NUMBER(c,p,i_pos,i_symb);
                        i_type = BLITZ_ARG_TYPE_NUM;
                        i_len = i_pos;
                        if (i_pos!=0) ok = 1;
                    } else if (BLITZ_IS_ALPHA(symb)){
                        BLITZ_SCAN_ALNUM(c,p,i_pos,i_symb);
                        i_len = i_pos;
                        i_type = BLITZ_ARG_TYPE_BOOL;
                        if (i_pos!=0) {
                            ok = 1;
                            // FIXME
                            if ((i_len == 5) && ((0 == strncmp("FALSE",token,5) || (0 == strncmp("false",token,5))))) {
                                *ptr_token = 'f';
                            } else if ((i_len == 4) && ((0 == strncmp("TRUE",token,4) || (0 == strncmp("true",token,4))))){
                                *ptr_token = 't';
                            } else {
                                ok = 0;
                            }
                        }
                    }
                    if (BLITZ_DEBUG) 
                        php_printf("STATE_NEXT_ARG(2): %ld, %ld; c = \"%c\", i_pos = %ld\n", pos, len_text, *c, i_pos);

                    if (ok) {
                        pos += i_pos;
                        c = text + pos;
                        if (BLITZ_DEBUG) php_printf("STATE_NEXT_ARG(3): %ld, %ld; c = \"%c\", i_pos = %ld\n", pos, len_text, *c, i_pos);
                        if (BLITZ_ARG_TYPE_BOOL == i_type) {
                            ADD_CALL_ARGS(ptr_token, 1, BLITZ_ARG_TYPE_BOOL);
                        } else {
                            ADD_CALL_ARGS(token, i_len, i_type);
                        }

                        if (BLITZ_DEBUG) php_printf("STATE_NEXT: %ld, %ld; c = %c\n", pos, len_text, *c);
                        state = BLITZ_CALL_STATE_HAS_NEXT;
                    } else {
                        state = BLITZ_CALL_STATE_ERROR;
                    }
                    break;
                case BLITZ_CALL_STATE_HAS_NEXT:
                    BLITZ_SKIP_BLANK(c,i_pos,pos);
                    symb = *c;
                    if (BLITZ_DEBUG) php_printf("STATE_HAS_NEXT: %ld, %ld; c = %c\n", pos, len_text, *c);
                    if (symb == ',') {
                        state = BLITZ_CALL_STATE_NEXT_ARG;
                        ++c; ++pos;
                    } else if (symb == ')') {
                        state = BLITZ_CALL_STATE_FINISHED;
                        ++c; ++pos;
                        if (BLITZ_DEBUG) php_printf("STATE_HAS_NEXT(2): %ld, %ld; c = %c\n", pos, len_text, *c);
                    } else {
                        state = BLITZ_CALL_STATE_ERROR;
                    }
                    break;
                case BLITZ_CALL_STATE_FINISHED:
                    BLITZ_SKIP_BLANK(c,i_pos,pos);
                    if (BLITZ_DEBUG) php_printf("STATE_FINISHED(1): %ld, %ld; c = %c\n", pos, len_text, *c);
                    if (*c == ';') {
                        ++c; ++pos;
                    }
                    BLITZ_SKIP_BLANK(c,i_pos,pos);
                    if (BLITZ_DEBUG) php_printf("STATE_FINISHED(2): %ld, %ld; c = %c\n", pos, len_text, *c);
                    if (pos < len_text) { // when close tag contains whitespaces SKIP_BLANK will make pos>=len_text
                        if (BLITZ_DEBUG) php_printf("%ld <> %ld => error state\n", pos, len_text);
                        state = BLITZ_CALL_STATE_ERROR;
                    }
                    ok = 0;
                    break;
                default:
                    ok = 0;
                    break;
            }
        }
    }

    if (state != BLITZ_CALL_STATE_FINISHED) {
        *error = BLITZ_CALL_ERROR;
    } else if ((node->type == BLITZ_NODE_TYPE_IF) && (node->n_args<2 || node->n_args>3)) {
        *error = BLITZ_CALL_ERROR_IF;
    } else if ((node->type == BLITZ_NODE_TYPE_INCLUDE) && (node->n_args!=1)) {
        *error = BLITZ_CALL_ERROR_INCLUDE;
    }

    // 2DO: check arguments for wrappers (escape, date, trim)
}
/* }}} */


/* {{{ get_line_number(char *str, unsigned long pos) */
/*************************************************************************************************/
inline unsigned long get_line_number(char *str, unsigned long pos) {
/*************************************************************************************************/
    register char *p = str;
    register unsigned long i = pos;
    register unsigned int n = 0;
    p += i;

    if (i>0) {
        i++;
        while (i--) {
            if (*p == '\n') {
                n++;
            }
            p--;
        }
    }

    // humans like to start counting with 1, not 0
    n += 1; 

    return n;
}
/* }}} */

/* {{{ get_line_pos(char *str, unsigned long pos) */
/*************************************************************************************************/
inline unsigned long get_line_pos(char *str, unsigned long pos) {
/*************************************************************************************************/
    register char *p = str;
    register unsigned long i = pos;

    p += i;
    if (i>0) {
        while (--i) {
            if (*p == '\n') {
                return pos - i + 1;
            }
            p--;
        }
    }
    // humans like to start counting with 1, not 0
    return pos - i + 1; 
}
/* }}} */

/* {{{ add_child_to_parent */
/*************************************************************************************************/
inline int add_child_to_parent(
/*************************************************************************************************/
    tpl_node_struct **p_parent, 
    tpl_node_struct *i_node TSRMLS_DC) {
/*************************************************************************************************/
     unsigned int n_alloc = 0;
     tpl_node_struct *parent = *p_parent;

    // dynamical reallocation
    if (0 == parent->n_children_alloc) {
        n_alloc = BLITZ_NODE_ALLOC_CHILDREN_INIT;
        parent->children = (tpl_node_struct**)emalloc(n_alloc*sizeof(tpl_node_struct*));
    } else if (parent->n_children > (parent->n_children_alloc - 1)) {
        n_alloc = parent->n_children_alloc << 2;
        parent->children = (tpl_node_struct**)erealloc(parent->children, n_alloc*sizeof(tpl_node_struct*));
    }

    if (n_alloc>0) {
        if (!parent->children) {
            php_error_docref(NULL TSRMLS_CC, E_ERROR,
                "INTERNAL ERROR: unable to allocate memory for children"
            );
            return 0;
        }
        parent->n_children_alloc = n_alloc;
    }

    parent->children[parent->n_children] = i_node;
    ++parent->n_children;
    return 1;
}
/* }}} */

/* {{{ BLITZ_POS_COMPARE(const void* v1, const void* v2) */
/*************************************************************************************************/
inline int BLITZ_POS_COMPARE(const void* v1, const void* v2) {
/*************************************************************************************************/
    tag_pos *p1 = (tag_pos*)v1;
    tag_pos *p2 = (tag_pos*)v2;
    long diff = p1->pos - p2->pos;
    return (diff == 0) ? 0 : ((diff > 0) ? 1 : -1);
}
/* }}} */

/* {{{  blitz_analyse (blitz_tpl *tpl TSRMLS_DC) */
/*************************************************************************************************/
inline int blitz_analyse (blitz_tpl *tpl TSRMLS_DC) {
/*************************************************************************************************/
    unsigned int n_open = 0, n_close = 0, n_nodes = 0;
    unsigned int n_phpt_open = 0, n_phpt_close = 0;
    unsigned int i = 0, j = 0;
    unsigned int l_open_node = 0;
    unsigned int l_close_node = 0;
    unsigned int lexem_len = 0;
    unsigned int true_lexem_len = 0;
    unsigned int shift = 0;
    unsigned int n_alloc = 0;
    tpl_node_struct *i_node = NULL, *parent = NULL;
    tpl_node_struct **node_stack = NULL, **pp_node_stack = NULL; 
    unsigned char *body = NULL;
    unsigned char *plex = NULL;
    unsigned char i_error = 0;
    tag_pos *pos = NULL, *i_tag_pos = NULL;
    unsigned int pos_size = 0;
    unsigned int pos_alloc_size = 0;
    unsigned long i_pos = 0; 
    unsigned char expect_type_mask = 0, i_type = 0, i_prev_type = 0, i_is_open = 0, is_phpt_tag = 0;

    unsigned long current_open = 0, current_close, next_open = 0;

    if (!tpl->static_data.body || (0 == tpl->static_data.body_len)) {
        return 1;
    }

    pos = ecalloc(BLITZ_ALLOC_TAGS_STEP, sizeof(tag_pos));
    pos_alloc_size = BLITZ_ALLOC_TAGS_STEP;

    if (pos == NULL) {
        return -1;
    }

    // find open node positions
    body = tpl->static_data.body;
    blitz_bm_search (
        body, tpl->static_data.body_len, 
        tpl->static_data.config->open_node, tpl->static_data.config->l_open_node, BLITZ_TAG_POS_OPEN,
        &n_open, &pos, &pos_size, &pos_alloc_size TSRMLS_CC
    );


    // find close node positions
    body = tpl->static_data.body;
    blitz_bm_search (
        body, tpl->static_data.body_len,
        tpl->static_data.config->close_node, tpl->static_data.config->l_close_node, BLITZ_TAG_POS_CLOSE,
        &n_close, &pos, &pos_size, &pos_alloc_size TSRMLS_CC
    );


    if (BLITZ_SUPPORT_PHPT_TAGS_PARTIALLY) {
        body = tpl->static_data.body;
        blitz_bm_search (
            body, tpl->static_data.body_len,
            tpl->static_data.config->phpt_ctx_left, tpl->static_data.config->l_phpt_ctx_left, BLITZ_TAG_POS_PHPT_CTX_LEFT,
            &n_phpt_open, &pos, &pos_size, &pos_alloc_size TSRMLS_CC
        );
        n_open += n_phpt_open;

        // find close node positions
        body = tpl->static_data.body;
        blitz_bm_search (
            body, tpl->static_data.body_len,
            tpl->static_data.config->phpt_ctx_right, tpl->static_data.config->l_phpt_ctx_right, BLITZ_TAG_POS_PHPT_CTX_RIGHT,
            &n_phpt_close, &pos, &pos_size, &pos_alloc_size TSRMLS_CC
        );
        n_close += n_phpt_close;
    }

    qsort(pos, pos_size, sizeof(tag_pos), BLITZ_POS_COMPARE);

    if (BLITZ_DEBUG) {
        for(i=0; i<pos_size; i++) {
            php_printf("pos = %ld, %ld\n", pos[i].pos, pos[i].type);
        }
    }

    // "<!--", "-->"
    /* 2DO: easiest way - just search "<!--" as BLITZ_TAG_POS_OPEN and "-->" as BLITZ_TAG_POS_CLOSE */
    /* 2DO: the "correct" way is to search all php-templates tags:
        pos_open
        pos_close
        pos_ctx_open_left
        pos_ctx_open_right
        pos_ctx_close_left
        pos_ctx_close_right
    ANY OF THIS CAN BE EQUAL TO open_node OR close_node, SO THIS MUST BE FILTERED (!!!)
    */

    // allocate memory for nodes
    tpl->static_data.nodes = (tpl_node_struct*)emalloc(
        n_open*sizeof(tpl_node_struct)
    );

    if (!tpl->static_data.nodes) {
        php_error_docref(NULL TSRMLS_CC, E_ERROR, 
            "INTERNAL ERROR: unable to allocate memory for %u nodes", n_open
        );
        efree(pos);
        return 0;
    }

    // set correct pairs even if template has wrong grammar
    i = 0;
    n_nodes = 0;
    expect_type_mask = BLITZ_TAG_POS_OPEN | BLITZ_TAG_POS_PHPT_CTX_LEFT;
    for(i=0; i<pos_size; i++) {
        i_tag_pos = pos + i;
        i_pos = i_tag_pos->pos;
        i_type = i_tag_pos->type;
        i_is_open = 0; 
        switch (i_type) {
            case BLITZ_TAG_POS_OPEN:
                current_open = i_pos;
                l_open_node = tpl->static_data.config->l_open_node;
                l_close_node = tpl->static_data.config->l_close_node;
                i_is_open = 1;
                break;
            case BLITZ_TAG_POS_CLOSE:
                current_close = i_pos;
                l_open_node = tpl->static_data.config->l_open_node;
                l_close_node = tpl->static_data.config->l_close_node;
                break;
            case BLITZ_TAG_POS_PHPT_CTX_LEFT:
                current_open = i_pos;
                l_open_node = tpl->static_data.config->l_phpt_ctx_left;
                l_close_node = tpl->static_data.config->l_phpt_ctx_right;
                i_is_open = 1;
                break;
            case BLITZ_TAG_POS_PHPT_CTX_RIGHT:
                current_close = i_pos;
                l_open_node = tpl->static_data.config->l_phpt_ctx_left;
                l_close_node = tpl->static_data.config->l_phpt_ctx_right;
                break;
            default: 
                break;
        }

        if (BLITZ_DEBUG) {
            php_printf("type = %ld, check_result = %ld, expect_type_mask = %ld, is_open=%ld\n", 
                i_type, i_type & expect_type_mask, expect_type_mask, i_is_open);
        }

        if (i_type != (i_type & expect_type_mask)) {
            // compability with HTML comments 
            // 1) all unexpected errors after "<!--" tag are ignored (BLITZ_TAG_POS_PHPT_CTX_LEFT)
            // 2) all unexpected errors for "-->" tag are ignored (BLITZ_TAG_POS_PHPT_CTX_RIGHT)
            if (i_prev_type == BLITZ_TAG_POS_PHPT_CTX_LEFT) {
                // OK: should we do something here?
            } else if (i_type == BLITZ_TAG_POS_PHPT_CTX_RIGHT
                && ((i_prev_type == BLITZ_TAG_POS_PHPT_CTX_RIGHT) || (i_prev_type == BLITZ_TAG_POS_CLOSE)))
            {
                // in these constructs "--> blabla -->" or "}} blabla -->" last tag is always just a comment,
                // so we just skip if the current is BLITZ_TAG_POS_PHPT_CTX_RIGHT too.
                continue; 
            } else {
                php_error_docref(NULL TSRMLS_CC, E_WARNING,
                    "SYNTAX ERROR: unexpected tag (%s: line %lu, pos %lu)",
                     tpl->static_data.name, get_line_number(body,i_pos), get_line_pos(body,i_pos)
                );
                expect_type_mask = 255;
                i_prev_type = i_type;
                continue;
            }
        }

        if (0 == i_is_open) {
            current_close = i_pos;
            shift = current_open + l_open_node;
            lexem_len = current_close - shift;
            if (BLITZ_DEBUG) {
                php_printf("close_pos = %ld, shift = %ld, lexem_len = %ld\n", 
                    current_close, shift, lexem_len);
            }

            if (lexem_len>BLITZ_MAX_LEXEM_LEN) {
                if (i_type != BLITZ_TAG_POS_PHPT_CTX_RIGHT) { // HTML-comments fix
                    php_error_docref(NULL TSRMLS_CC, E_WARNING, 
                        "SYNTAX ERROR: lexem is too long (%s: line %lu, pos %lu)", 
                        tpl->static_data.name, get_line_number(body,current_open), get_line_pos(body,current_open)
                    );
                }
            } else if (lexem_len<=0) {
                php_error_docref(NULL TSRMLS_CC, E_WARNING, 
                    "SYNTAX ERROR: zero length lexem (%s: line %lu, pos %lu)", 
                    tpl->static_data.name, get_line_number(body,current_open), get_line_pos(body,current_open)
                );
            } else { // OK: parse 
                i_error = 0;
                plex = tpl->static_data.body + shift;
                j = lexem_len;
                true_lexem_len = 0;
                i_node = tpl->static_data.nodes + n_nodes;
                i_node->pos_in_list = n_nodes;
                is_phpt_tag = (i_type == BLITZ_TAG_POS_PHPT_CTX_RIGHT) ? 1 : 0;
                blitz_parse_call(plex, j, i_node, &true_lexem_len, tpl->static_data.config->var_prefix, is_phpt_tag, &i_error TSRMLS_CC);

                if (i_error) {
                    i_node->has_error = 1;
                    // skip errors on <!-- tags: this can be simply HTML-comments
                    if ((i_type == BLITZ_TAG_POS_PHPT_CTX_RIGHT)) {
                        expect_type_mask = 255;
                        continue;
                    }
                    if (i_error == BLITZ_CALL_ERROR) {
                        php_error_docref(NULL TSRMLS_CC, E_WARNING,
                            "SYNTAX ERROR: invalid method call (%s: line %lu, pos %lu)",
                            tpl->static_data.name, get_line_number(body,current_open), get_line_pos(body,current_open)
                        );
                    } else if (i_error == BLITZ_CALL_ERROR_IF) {
                        php_error_docref(NULL TSRMLS_CC, E_WARNING,
                            "SYNTAX ERROR: invalid <if> syntax, only 2 or 3 arguments allowed (%s: line %lu, pos %lu)",
                            tpl->static_data.name, get_line_number(body,current_open), get_line_pos(body,current_open)
                        );
                    } else if (i_error == BLITZ_CALL_ERROR_INCLUDE) {
                        php_error_docref(NULL TSRMLS_CC, E_WARNING,
                            "SYNTAX ERROR: invalid <inlcude> syntax, only 1 argument allowed (%s: line %lu, pos %lu)",
                            tpl->static_data.name, get_line_number(body,current_open), get_line_pos(body,current_open)
                        );
                    }
                } else { 
                    i_node->has_error = 0;
                } 

                i_node->n_children = 0;
                i_node->n_children_alloc = 0;
                i_node->children = NULL;
                i_node->pos_begin_shift = 0;
                i_node->pos_end_shift = 0;
                // if it's a context node - build tree 
                if (i_node->type == BLITZ_NODE_TYPE_BEGIN) {
                    // stack: allocate here (no mallocs for simple templates)
                    if (!node_stack) {
                        // allocate memory for node stack to build tree
                        node_stack = (tpl_node_struct**)emalloc(
                            (n_open+1)*sizeof(tpl_node_struct*)
                        );
                        *node_stack = NULL; // first element is NULL to make simple check if stack is empty
                        pp_node_stack = node_stack;
                    }

                    // begin: init node, get parent from stack, 
                    // add child to parent, put child to stack

                    if (*pp_node_stack) {
                        parent = *pp_node_stack;
                    }
                    ++pp_node_stack;
                    *pp_node_stack = i_node;

                    i_node->pos_begin = current_open;
                    i_node->pos_begin_shift = current_close + l_close_node;

                    // next 3 need finalization when END is detected
                    i_node->pos_end = current_close + l_close_node; // fake - needs finalization
                    i_node->pos_end_shift = 0;
                    i_node->lexem_len = true_lexem_len; 
                    
                    ++n_nodes;
                    if (parent) {
                        if (!add_child_to_parent(&parent,i_node TSRMLS_CC)) {
                            return 0;
                        }
                    }
                    parent = i_node;
                } else if (i_node->type == BLITZ_NODE_TYPE_END) {
                    // end: remove node from stack, finalize node: set true close positions and new type
                    if (pp_node_stack && *pp_node_stack) {
                        //
                        //    {{ begin    a }}                 bla       {{                  end a }}
                        //   ^--pos_begin      ^--pos_begin_shift        ^--pos_end_shift            ^--pos_end
                        //
                        parent = *pp_node_stack;
                        parent->pos_end_shift = current_open;
                        parent->pos_end = current_close + l_close_node;
                        parent->type = BLITZ_NODE_TYPE_CONTEXT;
                        --pp_node_stack;
                        parent = *pp_node_stack;
                    } else {
                        // error: end with no begin
                        i_node->has_error = 1;
                        php_error_docref(NULL TSRMLS_CC, E_WARNING,
                            "SYNTAX ERROR: end with no begin (%s: line %lu, pos %lu)", 
                            tpl->static_data.name, get_line_number(body,current_open),get_line_pos(body,current_open)
                        );
                    }
                } else { // just a var- or call-node
                    i_node->pos_begin = current_open;
                    i_node->pos_end = current_close + l_close_node;
                    i_node->lexem_len = true_lexem_len;
                    ++n_nodes;
                    // add node to parent 
                    if (parent) {
                        if (!add_child_to_parent(&parent,i_node TSRMLS_CC)) {
                            return 0;
                        }
                    }
                }
            }

        }

        i_prev_type = i_type;

        if (BLITZ_DEBUG) php_printf("%ld(%d)", expect_type_mask, i_is_open);
        if (i_is_open) {
            if (BLITZ_TAG_POS_PHPT_CTX_LEFT == i_type) {
                expect_type_mask = BLITZ_TAG_POS_PHPT_CTX_RIGHT;
            } else if (BLITZ_TAG_POS_OPEN == i_type) {
                expect_type_mask = BLITZ_TAG_POS_CLOSE;
            }
        } else {
            expect_type_mask = BLITZ_TAG_POS_OPEN | BLITZ_TAG_POS_PHPT_CTX_LEFT;
        }

        if (BLITZ_DEBUG) php_printf("-> %ld(%d)\n", expect_type_mask, i_is_open);
    }

    tpl->static_data.n_nodes = n_nodes;
    efree(pos);

    return 1;
}
/* }}} */

/* {{{ blitz_exec_wrapper */
/**********************************************************************************************************************/
inline int blitz_exec_wrapper (
/**********************************************************************************************************************/
    char **result,
    int *result_len,
    unsigned long flags,
    int args_num,
    char **args,
    int *args_len,
    char *tmp_buf TSRMLS_DC)
/**********************************************************************************************************************/
{
    // following wrappers are to be added: escape, date, gettext, something else?...
    if (flags == BLITZ_NODE_TYPE_WRAPPER_ESCAPE) {
        char *hint_charset = NULL;
        char *quote_str = args[1];
        int hint_charset_len = 0;
        long quote_style = ENT_QUOTES;
        int all = 0, wrong_format = 0, quote_str_len = 0;
        
        if (quote_str) {
            quote_str_len = strlen(quote_str);
            if (quote_str_len == 0) {
                wrong_format = 1;
            } else if (0 == strncmp("ENT_",quote_str,4)) {
                char *p_str = quote_str + 4;
                if (0 == strncmp("QUOTES",p_str,6)) {
                    quote_style = ENT_QUOTES;
                } else if (0 == strncmp("COMPAT",p_str,6)) {
                    quote_style = ENT_COMPAT;
                } else if (0 == strncmp("NOQUOTES",p_str,8)) {
                    quote_style = ENT_NOQUOTES;
                } else {
                    wrong_format = 1;
                }
            } else {
                wrong_format = 1;
            }
        }

        if (wrong_format) {
            php_error_docref(NULL TSRMLS_CC, E_WARNING,
                "escape format error (\"%s\"), available formats are ENT_QUOTES, ENT_COMPAT, ENT_NOQUOTES", quote_str
            );
            return 0;
        }

        *result = php_escape_html_entities(args[0], args_len[0], result_len, all, quote_style, hint_charset TSRMLS_CC);
    } else if (flags == BLITZ_NODE_TYPE_WRAPPER_DATE) {
// FIXME: make it work with Win32
#ifndef PHP_WIN32
#if PHP_API_VERSION >= 20041225
#define BLITZ_WITH_TIMELIB 1
#endif
#endif
        char *format = NULL;
        time_t timestamp = 0;
#if BLITZ_WITH_TIMELIB
        struct tm ta;
#else
        struct tm *ta = NULL, tmbuf;
#endif

#if BLITZ_WITH_TIMELIB
        timelib_time   *t;
        timelib_tzinfo *tzi;
        timelib_time_offset *offset = NULL;
#endif
        int gm = 0; // use localtime
        char *p = NULL;
        int i = 0;

        // check format
        if (args_num>0) {
            format = args[0];
        }

        if (!format) {
            php_error_docref(NULL TSRMLS_CC, E_WARNING,
                "date syntax error"
            );
            return 0;
        }

        // use second argument or get current time
        if (args_num == 1) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            timestamp = tv.tv_sec;
        } else {
            p = args[1];
            i = args_len[1];
            if (!p) {
                timestamp = 0;
            } else {
                while (BLITZ_IS_NUMBER(*p) && i--) {
                    p++;
                }
                if (i>0) { // string? try to parse
                    timestamp = php_parse_date(args[1], NULL);
                } else { // numirical - treat as unixtimestamp
                    timestamp = atol(args[1]);
                }
            }
        }

#if BLITZ_WITH_TIMELIB
        t = timelib_time_ctor();
        tzi = get_timezone_info(TSRMLS_C);

        t->tz_info = tzi;

        t->zone_type = TIMELIB_ZONETYPE_ID;
        timelib_unixtime2local(t, timestamp);

        ta.tm_sec   = t->s;
        ta.tm_min   = t->i;
        ta.tm_hour  = t->h;
        ta.tm_mday  = t->d;
        ta.tm_mon   = t->m - 1;
        ta.tm_year  = t->y - 1900;
        ta.tm_wday  = timelib_day_of_week(t->y, t->m, t->d);
        ta.tm_yday  = timelib_day_of_year(t->y, t->m, t->d);
        offset = timelib_get_time_zone_info(timestamp, tzi);

        ta.tm_isdst = offset->is_dst;
#if HAVE_TM_GMTOFF
        ta.tm_gmtoff = offset->offset;
#endif
#if HAVE_TM_ZONE
        ta.tm_zone = offset->abbr;
#endif
        timelib_time_dtor(t);
        timelib_time_offset_dtor(offset);
        *result_len = strftime(tmp_buf, BLITZ_TMP_BUF_MAX_LEN, format, &ta);
#else
        if (gm) {
            ta = php_gmtime_r(&timestamp, &tmbuf);
        } else {
            ta = php_localtime_r(&timestamp, &tmbuf);
        }
        *result_len = strftime(tmp_buf, BLITZ_TMP_BUF_MAX_LEN, format, ta);
#endif
        *result = tmp_buf;
    }

    return 1;
}
/* }}} */

/* {{{ blitz_fetch_var_by_path */
/**********************************************************************************************************************/
inline int blitz_fetch_var_by_path (
/**********************************************************************************************************************/
    zval ***zparam,
    char *lexem,
    int lexem_len,
    zval *params,
    HashTable *globals TSRMLS_DC) {
/**********************************************************************************************************************/
    char *path = NULL;
    int path_len = 0;
    register int i = 0, j = 0, last_pos = 0, key_len = 0, is_last = 0;
    char key[256];
    char root_found = 0;

    last_pos = 0;
    j = lexem_len - 1;
    // walk through the path
    for (i=0; i<lexem_len; i++, j--) {
        is_last = (j == 0);
        if (('.' == lexem[i]) || is_last) {
            key_len = i - last_pos + is_last;
            memcpy(key, lexem + last_pos, key_len);
            key[key_len] = '\x0';

            // try to get data by the key
            if (0 == root_found) { // globals or params?
                root_found = (params && (SUCCESS == zend_hash_find(Z_ARRVAL_P(params), key, key_len+1, (void **) zparam)));
                if (!root_found) {
                    root_found = (globals && (SUCCESS == zend_hash_find(globals, key, key_len+1, (void**)zparam)));
                    if (!root_found) {
                        return 0;
                    }
                }
            } else { // just propagate through elem
                if (Z_TYPE_PP(*zparam) == IS_ARRAY) {
                    if (SUCCESS != zend_hash_find(Z_ARRVAL_PP(*zparam),key,key_len+1,(void **) zparam)) {
                        return 0;
                    }
                } else if (Z_TYPE_PP(*zparam) == IS_OBJECT) {
                    if (SUCCESS != zend_hash_find(Z_OBJPROP_PP(*zparam),key,key_len+1,(void **) zparam)) {
                        return 0;
                    }
                } else {
                    return 0;
                }
            }
            root_found = 1;
            last_pos = i + 1;
        }
    }

    return 1;
}
/* }}} */

/* {{{ blitz_exec_predefined_method */
/**********************************************************************************************************************/
inline int blitz_exec_predefined_method (
/**********************************************************************************************************************/
    blitz_tpl *tpl,
    tpl_node_struct *node,
    zval *iteration_params,
    zval *id,
    unsigned char **result,
    unsigned char **p_result,
    unsigned long *result_len,
    unsigned long *result_alloc_len,
    char *tmp_buf TSRMLS_DC) {
/**********************************************************************************************************************/
    zval **z = NULL;
    unsigned long buf_len = 0, new_len = 0;
    HashTable *params = NULL;
    char is_var = 0, is_var_path = 0, is_found = 0;

    if (node->type == BLITZ_NODE_TYPE_IF) {
        char not_empty = 0;
        int predefined = -1;
        char i_arg = 0;
        call_arg *arg = NULL;

        arg = node->args;
        BLITZ_GET_PREDEFINED_VAR(tpl, arg->name, arg->len, predefined);
        if (predefined >=0) {
            if (predefined != 0) {
                not_empty = 1;
            }
        } else if (arg->type == BLITZ_ARG_TYPE_VAR) {
            BLITZ_ARG_NOT_EMPTY(*arg,Z_ARRVAL_P(iteration_params),not_empty);
            if (not_empty == -1) {
                BLITZ_ARG_NOT_EMPTY(*arg,tpl->hash_globals,not_empty);
            }
        } else if (arg->type == BLITZ_ARG_TYPE_VAR_PATH) {
            if(blitz_fetch_var_by_path(&z, arg->name, arg->len, iteration_params, tpl->hash_globals TSRMLS_CC)) {
                BLITZ_ZVAL_NOT_EMPTY(z, not_empty);
            }
        }

        // not_empty = [ 1: found, not empty | 0: found, empty | -1: not found, "empty" ]
        if (not_empty == 1) { 
            i_arg = 1;
        } else {
            if (node->n_args == 3) { // empty && if has 3 arguments
                i_arg = 2;
            } else { // empty && if has only 2 arguments
                return 1;
            }
        }

        arg = node->args + i_arg;
        is_var = (arg->type == BLITZ_ARG_TYPE_VAR);
        is_var_path = (arg->type == BLITZ_ARG_TYPE_VAR_PATH);
        is_found = 0;
        if (is_var || is_var_path) { // argument is variable 
            if (is_var &&
                (
                    (SUCCESS == zend_hash_find(Z_ARRVAL_P(iteration_params),arg->name,1+arg->len,(void**)&z))
                    || (SUCCESS == zend_hash_find(tpl->hash_globals,arg->name,1+arg->len,(void**)&z))
                ))
            {
                is_found = 1;
            }
            else if (is_var_path 
                && blitz_fetch_var_by_path(&z, arg->name, arg->len, iteration_params, tpl->hash_globals TSRMLS_CC))
            {
                is_found = 1;
            }

            if (is_found) {
                convert_to_string_ex(z);
                buf_len = Z_STRLEN_PP(z);
                BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,*p_result);
                *p_result = (char*)memcpy(*p_result,Z_STRVAL_PP(z),buf_len);
                *result_len += buf_len;
                p_result+=*result_len;
            }
        } else { // argument is string or number
            buf_len = (unsigned long)arg->len;
            BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,*p_result);
            *p_result = (char*)memcpy(*p_result,node->args[i_arg].name,buf_len);
            *result_len += buf_len;
            p_result+=*result_len;
        }
    } else if (node->type == BLITZ_NODE_TYPE_INCLUDE) {
        char *filename = node->args[0].name;
        int filename_len = node->args[0].len;
        unsigned char *inner_result = NULL;
        unsigned long inner_result_len = 0;
        blitz_tpl *itpl = NULL;

        if (!blitz_include_tpl_cached(tpl, filename, filename_len, iteration_params, &itpl TSRMLS_CC)) {
            return 0;
        }

        // parse
        if (blitz_exec_template(itpl,id,&inner_result,&inner_result_len TSRMLS_CC)) {
            BLITZ_REALLOC_RESULT(inner_result_len,new_len,*result_len,*result_alloc_len,*result,*p_result);
            *p_result = (char*)memcpy(*p_result,inner_result,inner_result_len);
            *result_len += inner_result_len;
            p_result+=*result_len;
        }

    } else if (node->type == BLITZ_NODE_TYPE_WRAPPER) {
        char *wrapper_args[BLITZ_WRAPPER_MAX_ARGS];
        int  wrapper_args_len[BLITZ_WRAPPER_MAX_ARGS];
        char *str = NULL;
        int str_len = 0;
        call_arg *p_arg = NULL;
        int i = 0;
        int wrapper_args_num = 0;

        for(i=0; i<BLITZ_WRAPPER_MAX_ARGS; i++) {
            wrapper_args[i] = NULL;
            wrapper_args_len[i] = 0;

            if (i<node->n_args) {
                p_arg = node->args + i;
                wrapper_args_num++;
                if (p_arg->type == BLITZ_ARG_TYPE_VAR) {
                    if ((SUCCESS == zend_hash_find(Z_ARRVAL_P(iteration_params),p_arg->name,1+p_arg->len,(void**)&z))
                        || (SUCCESS == zend_hash_find(tpl->hash_globals,p_arg->name,1+p_arg->len,(void**)&z)))
                    {
                        convert_to_string_ex(z);
                        wrapper_args[i] = Z_STRVAL_PP(z);
                        wrapper_args_len[i] = Z_STRLEN_PP(z);
                    }
                } else if (p_arg->type == BLITZ_ARG_TYPE_VAR_PATH) {
                    if (blitz_fetch_var_by_path(&z, p_arg->name, p_arg->len, iteration_params, tpl->hash_globals TSRMLS_CC)) {
                        convert_to_string_ex(z);
                        wrapper_args[i] = Z_STRVAL_PP(z);
                        wrapper_args_len[i] = Z_STRLEN_PP(z);
                    }
                } else {
                    wrapper_args[i] = p_arg->name;
                    wrapper_args_len[i] = p_arg->len;
                }
            }
        }

        if (blitz_exec_wrapper(&str, &str_len, node->flags, wrapper_args_num, wrapper_args, wrapper_args_len, tmp_buf TSRMLS_CC)) {
            BLITZ_REALLOC_RESULT(str_len, new_len, *result_len, *result_alloc_len, *result, *p_result);
            *p_result = (char*)memcpy(*p_result, str, str_len);
            *result_len += str_len;
            p_result+=*result_len;
        } else {
            return 0;
        }
    }

    return 1;
}
/* }}} */

/* {{{ blitz_exec_user_method */
/**********************************************************************************************************************/
inline int blitz_exec_user_method (
/**********************************************************************************************************************/
    blitz_tpl *tpl,
    tpl_node_struct *node,
    zval *iteration_params,
    zval *obj,
    unsigned char **result,
    unsigned char **p_result,
    unsigned long *result_len,
    unsigned long *result_alloc_len TSRMLS_DC) {
/**********************************************************************************************************************/
    zval *retval = NULL, *retval_copy = NULL, *zmethod = NULL, **zparam = NULL;
    int method_res = 0;
    unsigned long buf_len = 0, new_len = 0;
    zval ***args = NULL; 
    zval **pargs = NULL;
    unsigned int i = 0;
    call_arg *i_arg = NULL;
    char i_arg_type = 0;
    char cl = 0;
    int predefined = -1;
   
    MAKE_STD_ZVAL(zmethod);
    ZVAL_STRING(zmethod,node->lexem,1);

    // prepare arguments if needed
    /* 2DO: probably there's much more easy way to prepare arguments without two emalloc */
    if (node->n_args>0 && node->args) {
        // dirty trick to pass arguments
        // pargs are zval ** with parameter data
        // args just point to pargs
        args = emalloc(node->n_args*sizeof(zval*));
        pargs = emalloc(node->n_args*sizeof(zval));
        for(i=0; i<node->n_args; i++) {
            args[i] = NULL;
            ALLOC_INIT_ZVAL(pargs[i]);
            ZVAL_NULL(pargs[i]);
            i_arg  = node->args + i;
            i_arg_type = i_arg->type;
            if (i_arg_type == BLITZ_ARG_TYPE_VAR) {
                predefined = -1;
                BLITZ_GET_PREDEFINED_VAR(tpl, i_arg->name, i_arg->len, predefined);
                if (predefined>=0) {
                    ZVAL_LONG(pargs[i],(long)predefined);
                } else {
                    if ((SUCCESS == zend_hash_find(Z_ARRVAL_P(iteration_params),i_arg->name,1+i_arg->len,(void**)&zparam))
                        || (SUCCESS == zend_hash_find(tpl->hash_globals,i_arg->name,1+i_arg->len,(void**)&zparam)))
                    {
                        args[i] = zparam;
                    }
                }
            } else if (i_arg_type == BLITZ_ARG_TYPE_VAR_PATH) {
                if (blitz_fetch_var_by_path(&zparam, i_arg->name, i_arg->len, iteration_params, tpl->hash_globals TSRMLS_CC)) {
                    args[i] = zparam;
                }
            } else if (i_arg_type == BLITZ_ARG_TYPE_NUM) {
                ZVAL_LONG(pargs[i],atol(i_arg->name));
            } else if (i_arg_type == BLITZ_ARG_TYPE_BOOL) {
                cl = *i_arg->name;
                if (cl == 't') { 
                    ZVAL_TRUE(pargs[i]);
                } else if (cl == 'f') {
                    ZVAL_FALSE(pargs[i]);
                }
            } else { 
                ZVAL_STRING(pargs[i],i_arg->name,1);
            }
            if (!args[i]) args[i] = & pargs[i];
        }
    } 

    // call object method
    method_res = call_user_function_ex(
        CG(function_table), &obj,
        zmethod, &retval, node->n_args, args, 1, NULL TSRMLS_CC
    );

    if (method_res == FAILURE) { // failure:
        php_error_docref(NULL TSRMLS_CC, E_WARNING,
            "INTERNAL ERROR: calling user method \"%s\" failed, check if this method exists or parameters are valid", node->lexem
        );
    } else if (retval) { // retval can be empty even in success: method throws exception
        convert_to_string_ex(&retval);
        buf_len = Z_STRLEN_P(retval);
        BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,*p_result);
        *p_result = (char*)memcpy(*p_result,Z_STRVAL_P(retval), buf_len);
        *result_len += buf_len;
        p_result+=*result_len;
    }

    zval_dtor(zmethod);

    if (pargs) {
         for(i=0; i<node->n_args; i++) {
             zval_ptr_dtor(pargs + i);
         }
         efree(args);
         efree(pargs);
    }

    return 1;
}
/* }}} */

/* {{{ blitz_exec_var */
/**********************************************************************************************************************/
inline void blitz_exec_var (
/**********************************************************************************************************************/
    blitz_tpl *tpl,
    char *lexem,
    zval *params,
    unsigned char **result,
    unsigned long *result_len,
    unsigned long *result_alloc_len TSRMLS_DC) {
/**********************************************************************************************************************/
    // FIXME: there should be just node->lexem_len+1, but method.phpt test becomes broken. REMOVE STRLEN!
    unsigned int lexem_len = strlen(lexem);
    unsigned int lexem_len_p1 = lexem_len+1;
    unsigned long buf_len = 0, new_len = 0;
    zval **zparam;
    char *p_result = *result;
    int predefined = -1;
    char predefined_buf[BLITZ_PREDEFINED_BUF_LEN];
    char is_found = 0;

    BLITZ_GET_PREDEFINED_VAR(tpl, lexem, lexem_len, predefined);
    if (predefined>=0) {
        snprintf(predefined_buf, BLITZ_PREDEFINED_BUF_LEN, "%u", predefined);
        buf_len = strlen(predefined_buf);
        BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,p_result);
        p_result = (char*)memcpy(p_result, predefined_buf, buf_len);
        *result_len += buf_len;
        p_result+=*result_len;
    } else {
        if ( (params && (SUCCESS == zend_hash_find(Z_ARRVAL_P(params), lexem, lexem_len_p1, (void**)&zparam)))
            ||
            (tpl->hash_globals && (SUCCESS == zend_hash_find(tpl->hash_globals, lexem, lexem_len_p1, (void**)&zparam))) )
        {
            if (Z_TYPE_PP(zparam) != IS_STRING) {
                zval *p = NULL;
                MAKE_STD_ZVAL(p);
                *p = **zparam;
                zval_copy_ctor(p);
                convert_to_string_ex(&p);
                buf_len = Z_STRLEN_P(p);
                BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,p_result);
                p_result = (char*)memcpy(p_result,Z_STRVAL_P(p), buf_len);
            } else {
               buf_len = Z_STRLEN_PP(zparam);
               BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,p_result);
               p_result = (char*)memcpy(p_result,Z_STRVAL_PP(zparam), buf_len);
            }

            *result_len += buf_len;
            p_result+=*result_len;
        }
    }
}
/* }}} */

/* {{{ blitz_exec_var_path */
/**********************************************************************************************************************/
inline void blitz_exec_var_path (
/**********************************************************************************************************************/
    blitz_tpl *tpl,
    char *lexem,
    zval *params,
    unsigned char **result,
    unsigned long *result_len,
    unsigned long *result_alloc_len TSRMLS_DC) {
/**********************************************************************************************************************/
    // FIXME: there should be just node->lexem_len+1, but method.phpt test becomes broken. REMOVE STRLEN!
    unsigned int lexem_len = strlen(lexem);
    unsigned int lexem_len_p1 = lexem_len+1;
    unsigned long buf_len = 0, new_len = 0;
    char *p_result = *result;
    zval **elem = NULL;

    if (!blitz_fetch_var_by_path(&elem, lexem, lexem_len, params, tpl->hash_globals TSRMLS_CC))
        return;

    if (!elem)
        return;

    if (Z_TYPE_PP(elem) != IS_STRING) {
        zval *p = NULL;
        MAKE_STD_ZVAL(p);
        *p = **elem;
        zval_copy_ctor(p);
        convert_to_string_ex(&p);
        buf_len = Z_STRLEN_P(p);
        BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,p_result);
        p_result = (char*)memcpy(p_result,Z_STRVAL_P(p), buf_len);
    } else {
       buf_len = Z_STRLEN_PP(elem);
       BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,p_result);
       p_result = (char*)memcpy(p_result,Z_STRVAL_PP(elem), buf_len);
    }

    *result_len += buf_len;
    p_result+=*result_len;

}
/* }}} */

/* {{{ blitz_exec_context */
/**********************************************************************************************************************/
void blitz_exec_context (
/**********************************************************************************************************************/
    blitz_tpl *tpl,
    tpl_node_struct *node,
    zval *parent_params,
    zval *id,
    unsigned char **result,
    unsigned long *result_len,
    unsigned long *result_alloc_len TSRMLS_DC) {
/**********************************************************************************************************************/
    char *key = NULL;
    unsigned int key_len = 0;
    unsigned long key_index = 0;
    int check_key = 0;
    zval **ctx_iterations = NULL;
    zval **ctx_data = NULL;
    call_arg *arg = node->args;

    if (BLITZ_DEBUG) php_printf("context:%s\n",node->args->name);
    if (parent_params && (zend_hash_find(Z_ARRVAL_P(parent_params), arg->name, 1 + arg->len, (void**)&ctx_iterations) != FAILURE)) {
        if (BLITZ_DEBUG) php_printf("data found in parent params:%s\n",arg->name);
        if (Z_TYPE_PP(ctx_iterations) == IS_ARRAY && zend_hash_num_elements(Z_ARRVAL_PP(ctx_iterations))) {
            tpl_node_struct *subnodes = NULL;
            unsigned int n_subnodes = 0;
            if (BLITZ_DEBUG) php_printf("will walk through iterations\n");
            if (node->children) {
                subnodes = *node->children;
                n_subnodes = node->n_children;
            }
            zend_hash_internal_pointer_reset_ex(Z_ARRVAL_PP(ctx_iterations), NULL);
            check_key = zend_hash_get_current_key_ex(Z_ARRVAL_PP(ctx_iterations), &key, &key_len, &key_index, 0, NULL);
            if (BLITZ_DEBUG) php_printf("KEY_CHECK: %d\n", check_key);

            if (HASH_KEY_IS_STRING == check_key) {
                if (BLITZ_DEBUG) php_printf("KEY_CHECK: string\n");
                blitz_exec_nodes(tpl,subnodes,n_subnodes,id,
                    result,result_len,result_alloc_len,
                    node->pos_begin_shift,
                    node->pos_end_shift,
                    *ctx_iterations TSRMLS_CC
                );
            } else if (HASH_KEY_IS_LONG == check_key) {
                if (BLITZ_DEBUG) php_printf("KEY_CHECK: long\n");
                BLITZ_LOOP_INIT(tpl, zend_hash_num_elements(Z_ARRVAL_PP(ctx_iterations)));
                while (zend_hash_get_current_data_ex(Z_ARRVAL_PP(ctx_iterations),(void**) &ctx_data, NULL) == SUCCESS) {
                    if (BLITZ_DEBUG) {
                        php_printf("GO subnode, params:\n");
                        php_var_dump(ctx_data,0 TSRMLS_CC);
                    }
                    // mix of num/str errors: array(0=>array(), 'key' => 'val')
                    if (IS_ARRAY != Z_TYPE_PP(ctx_data)) {
                        php_error_docref(NULL TSRMLS_CC, E_WARNING,
                            "ERROR: You have a mix of numerical and non-numerical keys in the iteration set "
                            "(context: %s, line %lu, pos %lu), key was ignored",
                            node->args[0].name,
                            get_line_number(tpl->static_data.body, node->pos_begin),
                            get_line_pos(tpl->static_data.body, node->pos_begin_shift)
                        );
                        zend_hash_move_forward_ex(Z_ARRVAL_PP(ctx_iterations), NULL);
                        continue;
                    }
                    blitz_exec_nodes(tpl,subnodes,n_subnodes,id,
                        result,result_len,result_alloc_len,
                        node->pos_begin_shift,
                        node->pos_end_shift,
                        *ctx_data TSRMLS_CC
                    );
                    BLITZ_LOOP_INCREMENT(tpl);
                    zend_hash_move_forward_ex(Z_ARRVAL_PP(ctx_iterations), NULL);
                }
            } else {
                php_error_docref(NULL TSRMLS_CC, E_WARNING, "INTERNAL ERROR: non existant hash key");
            }
        }
    }
}
/* }}} */

/* {{{ blitz_exec_nodes */
/**********************************************************************************************************************/
int blitz_exec_nodes (
/**********************************************************************************************************************/
    blitz_tpl *tpl, 
    tpl_node_struct *nodes,
    unsigned int n_nodes,
    zval *id, 
    unsigned char **result,
    unsigned long *result_len, 
    unsigned long *result_alloc_len,
    unsigned long parent_begin,
    unsigned long parent_end,
    zval *parent_ctx_data TSRMLS_DC) {
/**********************************************************************************************************************/
    unsigned int i = 0, i_max = 0, i_processed = 0;
    unsigned char *p_result = NULL;
    unsigned long buf_len = 0, new_len = 0;
    unsigned long shift = 0, last_close = 0, current_open = 0;
    unsigned long l_close_node = 0;
    char *key = NULL;
    unsigned int key_len = 0;
    unsigned long key_index = 0;
    zval *parent_params = NULL;
    tpl_node_struct *node;
    int check_key = 0;

    // check parent data (once in the beginning) - user could put non-array here. 
    // if we use hash_find on non-array - we get segfaults.
    if (parent_ctx_data && Z_TYPE_P(parent_ctx_data) == IS_ARRAY) {
        parent_params = parent_ctx_data;
    }

    p_result = *result;
    shift = 0;
    last_close = parent_begin;

    // walk through nodes, build result for each, 1-level sub-node, skip N-level ones (node->pos_begin<last_close)
    i = 0;
    i_processed = 0;
    i_max = tpl->static_data.n_nodes;
    
    if (BLITZ_DEBUG) {
        php_printf("BLITZ_FUNCTION blitz_exec_nodes, i_max:%ld, n_nodes = %ld\n",i_max,n_nodes);
        if (parent_ctx_data) php_var_dump(&parent_ctx_data, 0 TSRMLS_CC);
        if (parent_params) php_var_dump(&parent_params, 0 TSRMLS_CC);
    }

    for (; i<i_max && i_processed<n_nodes; ++i) {
        node = nodes + i;
        if (BLITZ_DEBUG)
            php_printf("node:%s, %ld/%ld pos = %ld, lc = %ld\n",node->lexem, i, n_nodes, node->pos_begin, last_close);

        if (node->pos_begin<last_close) { // not optimal: just fix for node-tree
            if (BLITZ_DEBUG) php_printf("SKIPPED(%ld) %ld\n", i, tpl->static_data.n_nodes);
            continue;
        }
        current_open = node->pos_begin;

        // between nodes
        if (current_open>last_close) {
            buf_len = current_open - last_close;
            BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,p_result);
            p_result = (char*)memcpy(p_result, tpl->static_data.body + last_close, buf_len); 
            *result_len += buf_len;
            p_result+=*result_len;
        }

        if (node->lexem && !node->has_error) {
            if (node->type == BLITZ_NODE_TYPE_VAR) { 
                blitz_exec_var(tpl, node->lexem, parent_params, result, result_len, result_alloc_len TSRMLS_CC);
            } else if (node->type == BLITZ_NODE_TYPE_VAR_PATH) {
                blitz_exec_var_path(tpl, node->lexem, parent_params, result, result_len, result_alloc_len TSRMLS_CC);
            } else if (BLITZ_IS_METHOD(node->type)) {
                if (node->type == BLITZ_NODE_TYPE_CONTEXT) {
                    BLITZ_LOOP_MOVE_FORWARD(tpl);
                    blitz_exec_context(tpl, node, parent_params, id, result, result_len, result_alloc_len TSRMLS_CC);
                    BLITZ_LOOP_MOVE_BACK(tpl);
                } else {
                    zval *iteration_params = parent_params ? parent_params : NULL;
                    if (BLITZ_IS_PREDEF_METHOD(node->type)) {
                        blitz_exec_predefined_method(
                            tpl, node, iteration_params,id,
                            result,&p_result,result_len,result_alloc_len, tpl->tmp_buf TSRMLS_CC
                        );
                    } else {
                        blitz_exec_user_method(
                            tpl,node,iteration_params,id,
                            result,&p_result,result_len,result_alloc_len TSRMLS_CC
                        );
                    }
                }
            }
        } 
        last_close = node->pos_end;
        ++i_processed;
    }

    if (BLITZ_DEBUG)
        php_printf("D:b3  %ld,%ld,%ld\n",last_close,parent_begin,parent_end);

    if (parent_end>last_close) {
        buf_len = parent_end - last_close;
        BLITZ_REALLOC_RESULT(buf_len,new_len,*result_len,*result_alloc_len,*result,p_result);
        p_result = (char*)memcpy(p_result, tpl->static_data.body + last_close, buf_len);
        *result_len += buf_len;
        p_result+=*result_len;
    }

    if (BLITZ_DEBUG)
        php_printf("END NODES\n");

    return 1;
}
/* }}} */

/* {{{ blitz_populate_root */
/**********************************************************************************************************************/
inline blitz_populate_root (
/**********************************************************************************************************************/
    blitz_tpl *tpl TSRMLS_DC) {
/**********************************************************************************************************************/
    zval *empty_array;
    if (BLITZ_DEBUG) php_printf("will populate the root iteration\n");
    MAKE_STD_ZVAL(empty_array);
    array_init(empty_array);
    add_next_index_zval(tpl->iterations, empty_array);

    return 1;
}
/* }}} */

/* {{{ blitz_exec_template */
/**********************************************************************************************************************/
int blitz_exec_template (
/**********************************************************************************************************************/
    blitz_tpl *tpl,
    zval *id,
    unsigned char **result,
    unsigned long *result_len TSRMLS_DC) {
/**********************************************************************************************************************/
    unsigned long result_alloc_len = 0;

    // quick return if there was no nodes
    if (0 == tpl->static_data.n_nodes) {
        *result = tpl->static_data.body; // won't copy
        *result_len += tpl->static_data.body_len;
        return 2; // will not call efree on result
    }

    // build result, initial alloc of twice bigger than body
    *result_len = 0;
    result_alloc_len = 2*tpl->static_data.body_len; 
    *result = (char*)ecalloc(result_alloc_len,sizeof(char));
    if (!*result) {
        return 0;
    }

    if (0 == zend_hash_num_elements(Z_ARRVAL_P(tpl->iterations))) {
        blitz_populate_root(tpl TSRMLS_CC);
    }

    if (tpl->iterations) {
        zval **iteration_data = NULL;
        char *key;
        unsigned int key_len;
        unsigned long key_index;

        // if it's an array of numbers - treat this as single iteration and pass as a parameter
        // otherwise walk and iterate all the array elements
        zend_hash_internal_pointer_reset(Z_ARRVAL_P(tpl->iterations));
        if (HASH_KEY_IS_LONG != zend_hash_get_current_key_ex(Z_ARRVAL_P(tpl->iterations), &key, &key_len, &key_index, 0, NULL)) {
           blitz_exec_nodes(tpl,tpl->static_data.nodes,tpl->static_data.n_nodes,id,result,result_len,&result_alloc_len,0,tpl->static_data.body_len,tpl->iterations TSRMLS_CC);
        } else {
           BLITZ_LOOP_INIT(tpl, zend_hash_num_elements(Z_ARRVAL_P(tpl->iterations)));
           while (zend_hash_get_current_data(Z_ARRVAL_P(tpl->iterations), (void**) &iteration_data) == SUCCESS) {
               if (
                   HASH_KEY_IS_LONG != zend_hash_get_current_key_ex(Z_ARRVAL_P(tpl->iterations), &key, &key_len, &key_index, 0, NULL)
                   || IS_ARRAY != Z_TYPE_PP(iteration_data)) 
               {
                   zend_hash_move_forward(Z_ARRVAL_P(tpl->iterations));
                   continue;
               }
               blitz_exec_nodes(tpl,tpl->static_data.nodes,tpl->static_data.n_nodes,id,result,result_len,&result_alloc_len,0,tpl->static_data.body_len,*iteration_data TSRMLS_CC);
               BLITZ_LOOP_INCREMENT(tpl);
               zend_hash_move_forward(Z_ARRVAL_P(tpl->iterations));
           }
        }
    } 

    return 1;
}
/* }}} */

/* {{{ blitz_normalize_path */
/**********************************************************************************************************************/
inline int blitz_normalize_path(char **dest, char *local, int local_len, char *global, int global_len TSRMLS_DC) {
/**********************************************************************************************************************/
    int buf_len = 0;
    char *buf = *dest;
    register char  *p = NULL, *q = NULL;

    if (local_len && local[0] == '/') {
        if (local_len+1>BLITZ_CONTEXT_PATH_MAX_LEN) {
            php_error_docref(NULL TSRMLS_CC, E_ERROR,"context path %s is too long (limit %d)",local,BLITZ_CONTEXT_PATH_MAX_LEN);
            return 0;
        }
        memcpy(buf, local, local_len+1);
        buf_len = local_len;
    } else {
        if (local_len + global_len + 2 > BLITZ_CONTEXT_PATH_MAX_LEN) {
            php_error_docref(NULL TSRMLS_CC, E_ERROR,"context path %s is too long (limit %d)",local,BLITZ_CONTEXT_PATH_MAX_LEN);
            return 0;
        }

        memcpy(buf, global, global_len);
        buf[global_len] = '/';
        buf_len = 1 + global_len;
        if (local) {
            memcpy(buf + 1 + global_len, local, local_len + 1);
            buf_len += local_len;
        }
    }

    buf[buf_len] = '\x0';
    while ((p = strstr(buf, "//"))) {
        for(q = p+1; *q; q++) *(q-1) = *q;
        *(q-1) = 0;
        --buf_len;
    }

    /* check for `..` in the path */
    /* first, remove path elements to the left of `..` */
    for(p = buf; p <= (buf+buf_len-3); p++) {
        if (memcmp(p, "/..", 3) != 0 || (*(p+3) != '/' && *(p+3) != 0)) continue;
        for(q = p-1; q >= buf && *q != '/'; q--, buf_len--);
        --buf_len;
        if (*q == '/') {
            p += 3;
            while (*p) *(q++) = *(p++);
            *q = 0;
            buf_len -= 3;
            p = buf;
        }
    }

    /* second, clear all `..` in the begining of the path
       because `/../` = `/` */
    while (buf_len > 2 && memcmp(buf, "/..", 3) == 0) {
        for(p = buf+3; *p; p++) *(p-3) = *p;
        *(p-3) = 0;
        buf_len -= 3;
    }

    /* clear `/` at the end of the path */
    while (buf_len > 1 && buf[buf_len-1] == '/') buf[--buf_len] = 0;
    if (!buf_len) { memcpy(buf, "/", 2); buf_len = 1; }

    buf[buf_len] = '\x0';

    return 1;
}
/* }}} */

/* {{{ blitz_iterate_by_path */
/**********************************************************************************************************************/
inline int blitz_iterate_by_path(
/**********************************************************************************************************************/
    blitz_tpl *tpl, 
    char *path, 
    int path_len, 
    int is_current_iteration, 
    int create_new TSRMLS_DC) {
/**********************************************************************************************************************/
    zval **tmp;
    int i = 1, ilast = 1, j = 0, k = 0;
    char *p = path;
    int pmax = path_len;
    char key[BLITZ_CONTEXT_PATH_MAX_LEN];
    int key_len  = 0;
    int found = 1; // create new iteration (add new item to parent list)
    int res = 0;
    long index = 0;
    int is_root = 0;

    k = pmax - 1;
    tmp = &tpl->iterations;

    if (BLITZ_DEBUG) {
        php_printf("[debug] BLITZ_FUNCTION: blitz_iterate_by_path, path=%s\n", path);
        php_printf("%p %p %p\n", tpl->current_iteration, tpl->last_iteration, tpl->iterations);
        if (tpl->current_iteration) php_var_dump(tpl->current_iteration,1 TSRMLS_CC);
    }

    // check root
    if (*p == '/' && pmax == 1) {
        is_root = 1;
    }

    if ((0 == zend_hash_num_elements(Z_ARRVAL_PP(tmp)) || (is_root && create_new))) {
        blitz_populate_root(tpl TSRMLS_CC);
    }

    // iterate root 
    if (is_root) {
        zend_hash_internal_pointer_end(Z_ARRVAL_PP(tmp));
        if (SUCCESS == zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &tpl->last_iteration)) {
            if (is_current_iteration) {
                //zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &tpl->current_iteration);
                tpl->current_iteration = tpl->last_iteration;
                tpl->current_iteration_parent = & tpl->iterations;
            } 
            if (BLITZ_DEBUG) {
                php_printf("last iteration becomes:\n");
                if (tpl->last_iteration) {
                    php_var_dump(tpl->last_iteration,1 TSRMLS_CC);
                } else {
                    php_printf("empty\n");
                }
            }
        } else {
            php_error_docref(NULL TSRMLS_CC, E_ERROR,
                "INTERNAL ERROR: zend_hash_get_current_data failed (0) in blitz_iterate_by_path");
            return 0;
        }
        return 1;
    }

    *p++;
    while (i<pmax) {
        if (*p == '/' || i == k) {
            j = i - ilast;
            key_len = j + (i == k ? 1 : 0);
            memcpy(key, p-j, key_len);
            key[key_len] = '\x0';

            if (BLITZ_DEBUG) php_printf("[debug] move to:%s\n",key);

            zend_hash_internal_pointer_end(Z_ARRVAL_PP(tmp));
            if (SUCCESS != zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &tmp)) {
                zval *empty_array;
                if (BLITZ_DEBUG) php_printf("[debug] current_data not found, will populate the list \n");
                MAKE_STD_ZVAL(empty_array);
                array_init(empty_array);
                add_next_index_zval(*tmp,empty_array);
                if (SUCCESS != zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &tmp)) {
                    php_error_docref(NULL TSRMLS_CC, E_ERROR,
                        "INTERNAL ERROR: zend_hash_get_current_data failed (1) in blitz_iterate_by_path");
                    return 0;
                }
                if (BLITZ_DEBUG) {
                    php_printf("[debug] tmp becomes:\n");
                    php_var_dump(tmp,0 TSRMLS_CC);
                }
            } else {
                if (BLITZ_DEBUG) {
                    php_printf("[debug] tmp dump (node):\n");
                    php_var_dump(tmp,0 TSRMLS_CC);
                }
            }


            if (SUCCESS != zend_hash_find(Z_ARRVAL_PP(tmp),key,key_len+1,(void **)&tmp)) {
                zval *empty_array;
                zval *init_array;

                if (BLITZ_DEBUG) php_printf("[debug] key \"%s\" was not found, will populate the list \n",key);
                found = 0;

                // create
                MAKE_STD_ZVAL(empty_array);
                array_init(empty_array);

                MAKE_STD_ZVAL(init_array);
                array_init(init_array);
                // [] = array(0 => array())
                if (BLITZ_DEBUG) {
                    php_printf("D-1: %p %p\n", tpl->current_iteration, tpl->last_iteration);
                    if (tpl->current_iteration) php_var_dump(tpl->current_iteration,1 TSRMLS_CC);
                }

                add_assoc_zval_ex(*tmp, key, key_len+1, init_array);

                if (BLITZ_DEBUG) {
                    php_printf("D-2: %p %p\n", tpl->current_iteration, tpl->last_iteration);
                   if (tpl->current_iteration) php_var_dump(tpl->current_iteration,1 TSRMLS_CC);
                }

                add_next_index_zval(init_array,empty_array);
                zend_hash_internal_pointer_end(Z_ARRVAL_PP(tmp));

                if (BLITZ_DEBUG) {
                    php_var_dump(tmp,0 TSRMLS_CC);
                }

                // 2DO: getting tmp and current_iteration_parent can be done by 1 call of zend_hash_get_current_data
                if (is_current_iteration) {
                    if (SUCCESS != zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &tpl->current_iteration_parent)) {
                        php_error_docref(NULL TSRMLS_CC, E_ERROR,
                            "INTERNAL ERROR: zend_hash_get_current_data failed (2) in blitz_iterate_by_path");
                        return 0;
                    }
                }

                if (SUCCESS != zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &tmp)) {
                    php_error_docref(NULL TSRMLS_CC, E_ERROR,
                        "INTERNAL ERROR: zend_hash_get_current_data failed (3) in blitz_iterate_by_path");
                    return 0;
                }

            }

            if (Z_TYPE_PP(tmp) != IS_ARRAY) {
                php_error_docref(NULL TSRMLS_CC, E_WARNING, 
                    "OPERATION ERROR: unable to iterate \"%s\" (sub-path of \"%s\"), "
                    "it was set as \"scalar\" value - check your iteration params", key, path);
                return 0;
            }

            ilast = i + 1;
            if (BLITZ_DEBUG) {
                php_printf("[debug] tmp dump (item \"%s\"):\n",key);
                php_var_dump(tmp,0 TSRMLS_CC);
            }
        }
        ++p;
        ++i;
    }

    /* logic notes: 
        - new iteration can be created while propagating through the path - then created 
          inside upper loop and found set to 0.
        - new iteration can be created if not found while propagating through the path and 
          called from block or iterate - then create_new=1 is used
        - in most cases new iteration will not be created if called from set, not found while 
          propagating through the path - then create_new=0 is used.
          but when we used fetch and then set to the same context - it is cleaned, 
          and there are no iterations at all.
          so in this particular case we do create an empty iteration.
    */

    if (found && (create_new || 0 == zend_hash_num_elements(Z_ARRVAL_PP(tmp)))) {
        zval *empty_array;
        MAKE_STD_ZVAL(empty_array);
        array_init(empty_array);

        add_next_index_zval(*tmp, empty_array);
        zend_hash_internal_pointer_end(Z_ARRVAL_PP(tmp));

        if (BLITZ_DEBUG) {
            php_printf("[debug] found, will add new iteration\n");
            php_var_dump(tmp,0 TSRMLS_CC);
        }
    }

    if (SUCCESS != zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &tpl->last_iteration)) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "INTERNAL ERROR: unable fetch last_iteration in blitz_iterate_by_path");
        tpl->last_iteration = NULL;
    }

    if (is_current_iteration) {
        tpl->current_iteration = tpl->last_iteration; // was: fetch from tmp
    }

    if (BLITZ_DEBUG) {
        php_printf("Iteration pointers: %p %p %p\n", tpl->current_iteration, tpl->current_iteration_parent, tpl->last_iteration);
        tpl->current_iteration ? php_var_dump(tpl->current_iteration,1 TSRMLS_CC) : php_printf("current_iteration is empty\n");
        tpl->current_iteration_parent ? php_var_dump(tpl->current_iteration_parent,1 TSRMLS_CC) : php_printf("current_iteration_parent is empty\n");
        tpl->last_iteration ? php_var_dump(tpl->last_iteration,1 TSRMLS_CC) : php_printf("last_iteration is empty\n");
    }

    return 1;
}
/* }}} */

/* {{{ blitz_find_iteration_by_path */
/**********************************************************************************************************************/
int blitz_find_iteration_by_path(
/**********************************************************************************************************************/
    blitz_tpl *tpl, 
    char *path, 
    int path_len, 
    zval **iteration, 
	zval **iteration_parent TSRMLS_DC) {
/**********************************************************************************************************************/
    zval **tmp, **entry;
    register int i = 1;
    int ilast = 1, j = 0, k = 0, key_len = 0;
    register char *p = path;
    register int pmax = path_len;
    char buffer[BLITZ_CONTEXT_PATH_MAX_LEN];
    char *key = NULL;
    zval **dummy;
    long index = 0;

    k = pmax - 1;
    tmp = & tpl->iterations;
    key = buffer;

    // check root
    if (BLITZ_DEBUG) php_printf("D:-0 %s(%ld)\n", path, path_len);

    if (*p == '/' && pmax == 1) {
        if (0 == zend_hash_num_elements(Z_ARRVAL_PP(tmp))) {
            blitz_populate_root(tpl TSRMLS_CC);
        }
        *iteration = NULL;
        zend_hash_internal_pointer_end(Z_ARRVAL_PP(tmp));
        if (SUCCESS != zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &entry)) {
            return 0;
        }
        *iteration = *entry;
        *iteration_parent = tpl->iterations;
        return 1;
    }

    if (i>=pmax) {
        return 0;
    }

    *p++;
    if (BLITZ_DEBUG) php_printf("%d/%d\n", i, pmax);
    while (i<pmax) {
        if (BLITZ_DEBUG) php_printf("%d/%d\n", i, pmax);
        if (*p == '/' || i == k) {
            j = i - ilast;
            key_len = j + (i == k ? 1 : 0);
            memcpy(key, p-j, key_len);
            key[key_len] = '\x0';
            if (BLITZ_DEBUG) php_printf("key:%s\n", key);

            // check the last key: if numerical - move to the end of array. otherwise search in this array.
            // this logic handles two iteration formats: 
            // 'parent' => [0 => {'child' => {'var' => 'val'}] (parent iterations is array with numerical indexes)
            // and 'block' => {'child' => {'var' => 'val'}} (just an assosiative array)
            zend_hash_internal_pointer_end(Z_ARRVAL_PP(tmp));
            if (zend_hash_get_current_key(Z_ARRVAL_PP(tmp), &key, &index, 0) == HASH_KEY_IS_LONG) {
                if (SUCCESS != zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &entry)) {
                    return 0;
                }
                if (BLITZ_DEBUG) {
                    php_printf("moving to the last array element:\n");
                    php_var_dump(entry, 0 TSRMLS_CC);
                }
                if (SUCCESS != zend_hash_find(Z_ARRVAL_PP(entry),key,key_len+1,(void **) &tmp)) {
                    return 0;
                }
            } else {
                if (SUCCESS != zend_hash_find(Z_ARRVAL_PP(tmp),key,key_len+1,(void **) &tmp)) {
                    return 0;
                }
            }

            if (BLITZ_DEBUG) {
                php_printf("key %s: we are here:\n");
                php_var_dump(tmp, 0 TSRMLS_CC);
            }

            ilast = i + 1;
        }
        ++p;
        ++i;
    }

    // can be not an array (tried to iterate scalar)
    if (IS_ARRAY != Z_TYPE_PP(tmp)) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "ERROR: unable to find context '%s', "
            "it was set as \"scalar\" value - check your iteration params", path); 
        return 0;
    }

    zend_hash_internal_pointer_end(Z_ARRVAL_PP(tmp));

    // if not array - stop searching, otherwise get the latest data
    if (zend_hash_get_current_key(Z_ARRVAL_PP(tmp), &key, &index, 0) == HASH_KEY_IS_STRING) {
        *iteration = *tmp;
        *iteration_parent = *tmp;
        return 1;
    }

    if (SUCCESS == zend_hash_get_current_data(Z_ARRVAL_PP(tmp), (void **) &dummy)) {
        if (BLITZ_DEBUG) php_printf("%p %p %p %p\n", dummy, *dummy, iteration, *iteration);
        *iteration = *dummy;
        *iteration_parent = *tmp;
    } else {
        return 0;
    }

    if (BLITZ_DEBUG) {
        php_printf("%p %p %p %p\n", dummy, *dummy, iteration, *iteration);
        php_printf("parent:\n");
        php_var_dump(iteration_parent,0 TSRMLS_CC);
        php_printf("found:\n");
        php_var_dump(iteration,0 TSRMLS_CC);
    }

    return 1;
}
/* }}} */


/* {{{ blitz_build_fetch_index_node */
/**********************************************************************************************************************/
void blitz_build_fetch_index_node(blitz_tpl *tpl, tpl_node_struct *node, char *path, unsigned int path_len) {
/**********************************************************************************************************************/
    unsigned long j = 0;
    unsigned int current_path_len = 0;
    char current_path[1024] = "";
    char *lexem = NULL;
    unsigned int lexem_len = 0;
    zval *temp = NULL;

    if (!node) return;
    if (path_len>0) memcpy(current_path,path,path_len);
    if (node->type == BLITZ_NODE_TYPE_CONTEXT) {
        lexem = node->args[0].name;
        lexem_len = node->args[0].len;
    } else if (BLITZ_IS_VAR(node->type)) {
        lexem = node->lexem;
        lexem_len = node->lexem_len;
    } else {
        return;
    }

    memcpy(current_path + path_len,"/",1);
    memcpy(current_path + path_len + 1, lexem, lexem_len);

    current_path_len = strlen(current_path);
    current_path[current_path_len] = '\x0';

    if (BLITZ_DEBUG) php_printf("building index for fetch_index, path=%s\n", current_path);

    MAKE_STD_ZVAL(temp);
    ZVAL_LONG(temp, node->pos_in_list);
    zend_hash_update(tpl->static_data.fetch_index, current_path, current_path_len+1, &temp, sizeof(zval *), NULL);

    if (node->children) {
        for (j=0;j<node->n_children;++j) {
           blitz_build_fetch_index_node(tpl, node->children[j], current_path, current_path_len);
        }
    }

}
/* }}} */

/* {{{ blitz_build_fetch_index(blitz_tpl *tpl TSRMLS_DC) */
/**********************************************************************************************************************/
int blitz_build_fetch_index(blitz_tpl *tpl TSRMLS_DC) {
/**********************************************************************************************************************/
    unsigned long i = 0, last_close = 0;
    char path[1024] = "";
    unsigned int path_len = 0;
    tpl_node_struct *i_node = NULL;
    ALLOC_HASHTABLE(tpl->static_data.fetch_index);
    if (!tpl->static_data.fetch_index || (FAILURE == zend_hash_init(tpl->static_data.fetch_index, 8, NULL, ZVAL_PTR_DTOR, 0))) {
        php_error_docref(NULL TSRMLS_CC, E_ERROR, "INTERNAL ERROR: unable to allocate or fill memory for blitz fetch index");
        return 0;
    }

    for (i=0;i<tpl->static_data.n_nodes;i++) {
        i_node = tpl->static_data.nodes + i;
        if (i_node->pos_begin>=last_close) {
            blitz_build_fetch_index_node(tpl, i_node, path, path_len);
            last_close = i_node->pos_end;
        }
    }

// remove comments to test the index
/*
php_printf("fetch_index dump:\n");

    HashTable *ht = tpl->fetch_index;
    zend_hash_internal_pointer_reset(ht);
    zval *elem = NULL;
    char *key = NULL;
    int key_len = 0;
    long index = 0;

    while (zend_hash_get_current_data(ht, (void**) &elem) == SUCCESS) {
        if (zend_hash_get_current_key_ex(ht, &key, &key_len, &index, 0, NULL) != HASH_KEY_IS_STRING) {
            zend_hash_move_forward(ht);
            continue;
        }
        php_printf("key: %s\n", key);
        php_var_dump(elem,0 TSRMLS_CC);
        zend_hash_move_forward(ht);
    }
*/
    return 1;
}
/* }}} */

/* {{{ blitz_touch_fetch_index */
/**********************************************************************************************************************/
inline int blitz_touch_fetch_index(blitz_tpl *tpl TSRMLS_DC) {
/**********************************************************************************************************************/
    if (!(tpl->flags & BLITZ_FLAG_FETCH_INDEX_BUILT)) {
        if (blitz_build_fetch_index(tpl TSRMLS_CC)) {
            tpl->flags |= BLITZ_FLAG_FETCH_INDEX_BUILT;
        } else { 
            return 0;
        } 
    }

    return 1;
}
/* }}} */

/* {{{ blitz_fetch_node_by_path */
/**********************************************************************************************************************/
int blitz_fetch_node_by_path(
/**********************************************************************************************************************/
    blitz_tpl *tpl, 
    zval *id, 
    char *path, 
    unsigned int path_len, 
    zval *input_params,
    unsigned char **result, 
    unsigned long *result_len TSRMLS_DC) {
/**********************************************************************************************************************/
    tpl_node_struct *i_node = NULL;
    unsigned long result_alloc_len = 0;
    zval **z = NULL;

    if ((path[0] == '/') && (path_len == 1)) {
        return blitz_exec_template(tpl,id,result,result_len TSRMLS_CC);
    }

    if (!blitz_touch_fetch_index(tpl TSRMLS_CC)) {
        return 0;
    }

    // find node by path  
    if (SUCCESS == zend_hash_find(tpl->static_data.fetch_index,path,path_len+1,(void**)&z)) {
        i_node = tpl->static_data.nodes + Z_LVAL_PP(z);
    } else {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "cannot find context %s in template %s", path, tpl->static_data.name);
        return 0;
    }

    // fetch result
    if (i_node->children) {
        result_alloc_len = 2*(i_node->pos_end_shift - i_node->pos_begin_shift);
        *result = (char*)ecalloc(result_alloc_len,sizeof(char));
        return blitz_exec_nodes(tpl,*i_node->children,i_node->n_children,id,
            result,result_len,&result_alloc_len,i_node->pos_begin_shift,i_node->pos_end_shift,
            input_params TSRMLS_CC
        );
    } else {
        unsigned long rlen = i_node->pos_end_shift - i_node->pos_begin_shift;
        *result_len = rlen;
        *result = (char*)emalloc(rlen+1);
        if (!*result)
            return 0;
        if (!memcpy(*result, tpl->static_data.body + i_node->pos_begin_shift, rlen)) {
            return 0;
        }
        *(*result + rlen) = '\x0';
    }

    return 1;
}
/* }}} */

/* {{{ blitz_iterate_current(blitz_tpl *tpl TSRMLS_DC) */
/**********************************************************************************************************************/
inline int blitz_iterate_current(blitz_tpl *tpl TSRMLS_DC) {
/**********************************************************************************************************************/
    zval *empty_array = NULL;

    if (BLITZ_DEBUG) php_printf("[debug] BLITZ_FUNCTION: blitz_iterate_current, path=%s\n", tpl->current_path);

    blitz_iterate_by_path(tpl, tpl->current_path, strlen(tpl->current_path), 1, 1 TSRMLS_CC);
    tpl->last_iteration = tpl->current_iteration;

    return 1;
}
/* }}} */

/* {{{ blitz_prepare_iteration(blitz_tpl *tpl, char *path, int path_len TSRMLS_DC) */
/**********************************************************************************************************************/
inline int blitz_prepare_iteration(blitz_tpl *tpl, char *path, int path_len, int iterate_nonexistant  TSRMLS_DC) {
/**********************************************************************************************************************/
    int res = 0;

    if (BLITZ_DEBUG) php_printf("[debug] BLITZ_FUNCTION: blitz_prepare_iteration, path=%s\n", path);

    if (path_len == 0) {
        return blitz_iterate_current(tpl TSRMLS_CC);
    } else {
        int current_len = strlen(tpl->current_path);
        int norm_len = 0;
        res = blitz_normalize_path(&tpl->tmp_buf, path, path_len, tpl->current_path, current_len TSRMLS_CC);

        if (!res) return 0;
        norm_len = strlen(tpl->tmp_buf);

        // check if path exists
        if (norm_len>1) {
            zval *dummy = NULL;
            if (!blitz_touch_fetch_index(tpl TSRMLS_CC)) {
                return 0;
            }

            if ((0 == iterate_nonexistant) 
                && SUCCESS != zend_hash_find(tpl->static_data.fetch_index,tpl->tmp_buf,norm_len+1,(void**)&dummy))
            {
                return -1;
            }
        }

        if (tpl->current_iteration_parent
            && (current_len == norm_len)
            && (0 == strncmp(tpl->tmp_buf,tpl->current_path,norm_len)))
        {
            return blitz_iterate_current(tpl TSRMLS_CC);
        } else {
            return blitz_iterate_by_path(tpl, tpl->tmp_buf, norm_len, 0, 1 TSRMLS_CC);
        }
    }

    return 0;
}
/* }}} */

/* {{{ blitz_merge_iterations_by_str_keys */
/**********************************************************************************************************************/
inline int blitz_merge_iterations_by_str_keys(
    zval **target, 
    zval *input TSRMLS_DC) { 
/**********************************************************************************************************************/
    zval **elem;
    HashTable *input_ht = NULL;
    char *key = NULL;
    int key_len = 0;
    long index = 0;
    int res = 0;

    if (!input || (IS_ARRAY != Z_TYPE_P(input))) {
        return 0;
    }

    if (0 == zend_hash_num_elements(Z_ARRVAL_P(input))) {
        return 1;
    }

    input_ht = Z_ARRVAL_P(input);
    while (zend_hash_get_current_data(input_ht, (void**) &elem) == SUCCESS) {
        if (zend_hash_get_current_key_ex(input_ht, &key, &key_len, &index, 0, NULL) != HASH_KEY_IS_STRING) {
            zend_hash_move_forward(input_ht);
            continue;
        }

        if (key) {
            zval *temp;
            ALLOC_INIT_ZVAL(temp);
            *temp = **elem;
            zval_copy_ctor(temp);

            res = zend_hash_update(
                Z_ARRVAL_PP(target),
                key,
                key_len,
                (void*)&temp, sizeof(zval *), NULL
            );
        }
        zend_hash_move_forward(input_ht);
    }

    return 1;
}
/* }}} */

/* {{{ blitz_merge_iterations_by_num_keys */
/**********************************************************************************************************************/
inline int blitz_merge_iterations_by_num_keys(
    zval **target,
    zval *input TSRMLS_DC) {
/**********************************************************************************************************************/
    zval **elem;
    HashTable *input_ht = NULL;
    char *key = NULL;
    int key_len = 0;
    long index = 0;
    int res = 0;

    if (!input || (IS_ARRAY != Z_TYPE_P(input))) {
        return 0;
    }

    if (0 == zend_hash_num_elements(Z_ARRVAL_P(input))) {
        return 1;
    }

    input_ht = Z_ARRVAL_P(input);
    while (zend_hash_get_current_data(input_ht, (void**) &elem) == SUCCESS) {
        zval *temp;

        if (zend_hash_get_current_key_ex(input_ht, &key, &key_len, &index, 0, NULL) != HASH_KEY_IS_LONG) {
            zend_hash_move_forward(input_ht);
            continue;
        }

        ALLOC_INIT_ZVAL(temp);
        *temp = **elem;
        zval_copy_ctor(temp);

        zend_hash_index_update(
            Z_ARRVAL_PP(target),
            index,
            (void*)&temp, sizeof(zval *), NULL
        );

        zend_hash_move_forward(input_ht);
    }

    return 1;
}
/* }}} */

/* {{{ blitz_merge_iterations_by_num_keys */
/**********************************************************************************************************************/
inline int blitz_merge_iterations_set (
    blitz_tpl *tpl,
    zval *input_arr TSRMLS_DC) {
/**********************************************************************************************************************/
    HashTable *input_ht = NULL;
    char *key = NULL;
    int key_len = 0;
    long index = 0;
    int res = 0, is_current_iteration = 0, first_key_type = 0;
    zval **target_iteration;

    if (0 == zend_hash_num_elements(Z_ARRVAL_P(input_arr))) {
        return 0;
    }

    // *** DATA STRUCTURE FORMAT ***
    // set works differently for numerical keys and string keys:
    //     (1) STRING: set(array('a' => 'a_val')) will update current_iteration keys
    //     (2) LONG: set(array(0=>array('a'=>'a_val'))) will reset current_iteration_parent
    input_ht = Z_ARRVAL_P(input_arr);
    zend_hash_internal_pointer_reset(input_ht);
    first_key_type = zend_hash_get_current_key_ex(input_ht, &key, &key_len, &index, 0, NULL);

    // *** FIXME ***
    // blitz_iterate_by_path here should have is_current_iteration = 1 ALWAYS.
    // BUT for some reasons this broke tests. Until the bug is found for the backward compatibility
    // is_current_iteration is 0 for string key set and 1 for numerical (otherwise current_iteration_parrent
    // is not set properly) in blitz_iterate_by_path

    //    is_current_iteration = 1;
    if (HASH_KEY_IS_LONG == first_key_type) is_current_iteration = 1;
    if (!tpl->current_iteration) {
        blitz_iterate_by_path(tpl, tpl->current_path, strlen(tpl->current_path), is_current_iteration, 0 TSRMLS_CC);
    } else {
        // fix last_iteration: if we did iterate('/some') before and now set in '/',
        // then current_iteration is not empty but last_iteration points to '/some'
        tpl->last_iteration = tpl->current_iteration;
    }

    zend_hash_internal_pointer_reset(Z_ARRVAL_PP(tpl->last_iteration));
    if (HASH_KEY_IS_STRING == first_key_type) {
        target_iteration = tpl->last_iteration;
        res = blitz_merge_iterations_by_str_keys(target_iteration, input_arr TSRMLS_CC);
    } else {
        if (!tpl->current_iteration_parent) {
            php_error_docref(NULL TSRMLS_CC, E_ERROR,
                "INTERNAL ERROR: unable to set into current_iteration_parent, is NULL"
            );
            return 0;
        }
        target_iteration = tpl->current_iteration_parent;
        zend_hash_clean(Z_ARRVAL_PP(target_iteration));
        tpl->current_iteration = NULL;  // parent was cleaned
        res = blitz_merge_iterations_by_num_keys(target_iteration, input_arr TSRMLS_CC);
    }

    return 1;
}
/* }}} */

/**********************************************************************************************************************
* Blitz CLASS methods
**********************************************************************************************************************/

/* {{{ blitz_init(filename) */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_init) {
/**********************************************************************************************************************/
    blitz_tpl *tpl = NULL;
    unsigned int filename_len = 0;
    char *filename = NULL;
    zval **desc = NULL;

    zval *new_object = NULL;
    int ret = 0;

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"|s",&filename,&filename_len)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    // initialize template 
    if (!(tpl = blitz_init_tpl(filename, NULL, NULL TSRMLS_CC))) {
        blitz_free_tpl(tpl);    
        RETURN_FALSE;
    }

    if (filename) {
        // analyse template 
        if (!blitz_analyse(tpl TSRMLS_CC)) {
            blitz_free_tpl(tpl);
            RETURN_FALSE;
        }
    }

    MAKE_STD_ZVAL(new_object);

    if (object_init(new_object) != SUCCESS)
    {
        RETURN_FALSE;
    }

    ret = zend_list_insert(tpl, le_blitz);
    add_property_resource(getThis(), "tpl", ret);
    zend_list_addref(ret);

}
/* }}} */

/* {{{ blitz_load */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_load) {
/**********************************************************************************************************************/
    blitz_tpl *tpl = NULL;
    zval *body;
    zval *id = NULL;
    zval **desc = NULL;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"z",&body)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    convert_to_string_ex(&body);
    if (tpl->static_data.body) {
        php_error_docref(NULL TSRMLS_CC, E_WARNING,"INTERNAL ERROR: template is already loaded");
        RETURN_FALSE;
    }

    // load body
    if (!blitz_load_body(tpl, body TSRMLS_CC)) {
        blitz_free_tpl(tpl);
        RETURN_FALSE;
    }

    // analyse template
    if (!blitz_analyse(tpl TSRMLS_CC)) {
        blitz_free_tpl(tpl);
        RETURN_FALSE;
    }

    RETURN_TRUE;
}
/* }}} */

/* {{{ blitz_dump_struct */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_dump_struct) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);
    php_blitz_dump_struct(tpl);

    RETURN_TRUE;
}
/* }}} */

/* {{{ blitz_get_struct */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_get_struct) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    zval *list = NULL;
    blitz_tpl *tpl = NULL;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    if (FAILURE == array_init(return_value)) {
        RETURN_FALSE;
    }

    php_blitz_get_path_list(tpl, return_value);

    return;
}
/* }}} */

/* {{{ blitz_dump_iterations */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_dump_iterations) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    php_printf("ITERATION DUMP (4 parts)\n");
    php_printf("(1) iterations:\n");
    php_var_dump(&tpl->iterations,1 TSRMLS_CC);
    php_printf("(2) current path is: %s\n",tpl->current_path);
    php_printf("(3) current node data (current_iteration_parent) is:\n");
    if (tpl->current_iteration_parent && *tpl->current_iteration_parent) {
        php_var_dump(tpl->current_iteration_parent,1 TSRMLS_CC);
    } else {
        php_printf("empty\n");
    }
    php_printf("(4) current node item data (current_iteration) is:\n");
    if (tpl->current_iteration && *tpl->current_iteration) {
        php_var_dump(tpl->current_iteration,1 TSRMLS_CC);
    } else {
        php_printf("empty\n");
    }

    RETURN_TRUE;
}
/* }}} */

/* {{{ blitz_get_iterations */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_get_iterations) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    if (FAILURE == array_init(return_value)) {
        RETURN_FALSE;
    }

    if (tpl->iterations) {
        *return_value = *tpl->iterations; 
        zval_copy_ctor(return_value);
    }

    return;
}
/* }}} */

/* {{{ blitz_set_global(zend_array(node_key=>node_val)) */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_set_global) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;
    zval *input_arr = NULL, **elem;
    zval *temp = NULL;
    HashTable *input_ht = NULL;
    char *key = NULL;
    int key_len = 0;
    long index = 0;
    int r = 0;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"a",&input_arr)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    input_ht = Z_ARRVAL_P(input_arr);
    zend_hash_internal_pointer_reset(tpl->hash_globals);
    zend_hash_internal_pointer_reset(input_ht);

    while (zend_hash_get_current_data(input_ht, (void**) &elem) == SUCCESS) {
        if (zend_hash_get_current_key_ex(input_ht, &key, &key_len, &index, 0, NULL) != HASH_KEY_IS_STRING) {
            zend_hash_move_forward(input_ht);
            continue;
        } 

        ALLOC_INIT_ZVAL(temp);
        *temp = **elem;
        zval_copy_ctor(temp);

        r = zend_hash_update(
            tpl->hash_globals,
            key,
            key_len,
            (void*)&temp, sizeof(zval *), NULL 
        );
        zend_hash_move_forward(input_ht);
    }

    RETURN_TRUE;
}
/* }}} */

/* {{{ blitz_set_global(zend_array(node_key=>node_val)) */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_get_globals) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;
    HashTable *tmp_ht = NULL;
    zval *tmp = NULL;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    if (FAILURE == array_init(return_value)) {
        RETURN_FALSE;
    }

    ALLOC_HASHTABLE_REL(tmp_ht);
    zend_hash_init(tmp_ht, 0, NULL, ZVAL_PTR_DTOR, 0);
    zend_hash_copy(tmp_ht, tpl->hash_globals, (copy_ctor_func_t) zval_add_ref, (void *) &tmp, sizeof(zval *));
    return_value->value.ht = tmp_ht;

    return;
}
/* }}} */

/* {{{ has_context(context) */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_has_context) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    zval **z = NULL;
    blitz_tpl *tpl;
    char *path = NULL;
    int path_len = 0, norm_len = 0, current_len = 0;
    long index = 0;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s",&path,&path_len)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if ((path[0] == '/') && (path_len == 1)) {
        RETURN_TRUE;
    }

    current_len = strlen(tpl->current_path);
    if (!blitz_normalize_path(&tpl->tmp_buf, path, path_len, tpl->current_path, current_len TSRMLS_CC)) {
        RETURN_FALSE;
    }
    norm_len = strlen(tpl->tmp_buf);

    if (!blitz_touch_fetch_index(tpl TSRMLS_CC)) {
        RETURN_FALSE;
    }

    // find node by path
    if (SUCCESS == zend_hash_find(tpl->static_data.fetch_index,tpl->tmp_buf,norm_len+1,(void**)&z)) {
        RETURN_TRUE;
    }

    RETURN_FALSE;
}
/* }}} */

/* {{{ blitz_parse() */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_parse) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl;
    unsigned char *result = NULL;
    unsigned long result_len = 0;
    zval *input_arr = NULL;
    zval      **src_entry = NULL;
    char      *string_key = NULL;
    unsigned int      string_key_len = 0;
    unsigned long     num_key = 0;
    HashTable *input_ht = NULL;
    char res = 0;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"|z",&input_arr)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    } 

    if (!tpl->static_data.body) { // body was not loaded
        RETURN_FALSE;
    }

    if (input_arr && (IS_ARRAY == Z_TYPE_P(input_arr)) && (0 < zend_hash_num_elements(Z_ARRVAL_P(input_arr)))) {
        if (!blitz_merge_iterations_set(tpl, input_arr TSRMLS_CC)) {
            RETURN_FALSE;
        }
    } 

    res = blitz_exec_template(tpl, id, &result, &result_len TSRMLS_CC);

    if (res) {
        ZVAL_STRINGL(return_value, result, result_len, 1);
        if (res == 1) efree(result);
    } else {
        RETURN_FALSE;
    }

    return;
}
/* }}} */

/* {{{ blitz_context() */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_context) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;
    char *path = NULL;
    int current_len = 0, norm_len = 0, path_len = 0, res = 0;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s",&path,&path_len)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    current_len = strlen(tpl->current_path);
    ZVAL_STRINGL(return_value, tpl->current_path, current_len, 1);

    if (path && path_len == current_len && 0 == strncmp(path,tpl->current_path,path_len)) {
        return;
    }

    norm_len = 0;
    res = blitz_normalize_path(&tpl->tmp_buf, path, path_len, tpl->current_path, current_len TSRMLS_CC);
    if (res) {
        norm_len = strlen(tpl->tmp_buf);
    }

    if ((current_len != norm_len) || (0 != strncmp(tpl->tmp_buf,tpl->current_path,norm_len))) {
        tpl->current_iteration = NULL; 
    }

    tpl->current_path = estrndup(tpl->tmp_buf,norm_len);
}
/* }}} */

/* {{{ blitz_get_context() */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_get_context) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);
    ZVAL_STRINGL(return_value, tpl->current_path, strlen(tpl->current_path), 1);
}
/* }}} */

/* {{{ blitz_iterate() */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_iterate) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl = NULL;
    char *path = NULL;
    int path_len = 0;
    zval *p_iterate_nonexistant = NULL;
    int iterate_nonexistant = 0;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"|sz",&path, &path_len, &p_iterate_nonexistant)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (p_iterate_nonexistant)
        BLITZ_ZVAL_NOT_EMPTY(&p_iterate_nonexistant, iterate_nonexistant);

    if (blitz_prepare_iteration(tpl, path, path_len, iterate_nonexistant TSRMLS_CC)) {
        RETURN_TRUE;
    }

    RETURN_FALSE;
}
/* }}} */


/* {{{ blitz_set() */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_set) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl;
    zval *input_arr = NULL;
    HashTable *input_ht = NULL;
    char *key = NULL;
    int key_len = 0;
    long index = 0;
    int res = 0;
    int first_key_type = 0, is_current_iteration = 0;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"a",&input_arr)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    res = blitz_merge_iterations_set(tpl, input_arr TSRMLS_CC);

    if (res) {
        RETURN_TRUE;
    } else {
        RETURN_FALSE
    }
}
/* }}} */

/* {{{ blitz_block() */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_block) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl;
    zval *p1 = NULL, *p2 = NULL, *p_iterate_nonexistant = NULL, *input_arr = NULL;
    char *path = NULL;
    int path_len = 0;
    int iterate_nonexistant = 0;
    int res = 0;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"z|zz",&p1,&p2,&p_iterate_nonexistant)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (IS_STRING == Z_TYPE_P(p1)) {
        path = p1->value.str.val;
        path_len = p1->value.str.len;
        if (p2 && (IS_ARRAY == Z_TYPE_P(p2))) {
            input_arr = p2;
        }
    } else if (IS_NULL == Z_TYPE_P(p1)) {
        if (p2 && (IS_ARRAY == Z_TYPE_P(p2))) {
            input_arr = p2;
        }
    } else if (IS_ARRAY == Z_TYPE_P(p1)) {
        input_arr = p1;
    } else {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "block can accept NULL/string/array as first parameter only\n");
        RETURN_FALSE;
    }

    if (p_iterate_nonexistant)
        BLITZ_ZVAL_NOT_EMPTY(&p_iterate_nonexistant, iterate_nonexistant)

    res = blitz_prepare_iteration(tpl, path, path_len, iterate_nonexistant TSRMLS_CC);
    if (res == -1) { // context doesn't exist
        RETURN_TRUE;
    } else if (res == 0) { // error
        RETURN_FALSE;
    }

    // copy params array to current iteration
    if (input_arr && zend_hash_num_elements(Z_ARRVAL_P(input_arr))>0) {
        if (tpl->last_iteration && *tpl->last_iteration) {
            **(tpl->last_iteration) = *input_arr;
            zval_copy_ctor(*tpl->last_iteration);
        } else {
            php_error_docref(NULL TSRMLS_CC, E_WARNING, "INTERNAL ERROR: last_iteration is empty, it's a bug\n"); 
        }
    }

    RETURN_TRUE;
}
/* }}} */

/* {{{ blitz_incude(filename,params) */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_include) {
/**********************************************************************************************************************/
    blitz_tpl *tpl = NULL, *itpl = NULL;
    char *filename = NULL;
    int filename_len = 0;
    zval **desc = NULL;
    zval *id = NULL;
    unsigned char *result = NULL;
    unsigned long result_len = 0;
    zval *input_arr = NULL;
    zval **src_entry = NULL;
    char *string_key = NULL;
    unsigned int string_key_len = 0;
    unsigned long num_key = 0;
    HashTable *input_ht = NULL;
    int res = 0;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s|z",&filename,&filename_len,&input_arr)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (!filename) RETURN_FALSE;

    if (!blitz_include_tpl_cached(tpl, filename, filename_len, NULL, &itpl TSRMLS_CC)) {
        RETURN_FALSE;
    }

    if (input_arr && (IS_ARRAY == Z_TYPE_P(input_arr)) && (0 < zend_hash_num_elements(Z_ARRVAL_P(input_arr)))) {
        if (!blitz_merge_iterations_set(itpl, input_arr TSRMLS_CC)) {
            RETURN_FALSE;
        }
    }

    res = blitz_exec_template(itpl, id, &result, &result_len TSRMLS_CC);
    if (res) {
        ZVAL_STRINGL(return_value, result, result_len, 1);
        if (res == 1) efree(result);
    } else {
        RETURN_FALSE;
    }
}
/* }}} */

/* {{{ blitz_fetch(path,params) */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_fetch) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl;
    unsigned char *result = NULL;
    unsigned long result_len = 0;
    char exec_status = 0;
    char *path = NULL;
    int path_len = 0, norm_len = 0, res = 0, current_len = 0;
    zval *input_arr = NULL, **elem;
    HashTable *input_ht = NULL;
    char *key = NULL;
    int key_len = 0;
    long index = 0;
    zval *path_iteration = NULL;
    zval *path_iteration_parent = NULL;
    zval *final_params = NULL;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"s|z",&path,&path_len,&input_arr)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    if (input_arr && (IS_ARRAY != Z_TYPE_P(input_arr))) {
        input_arr = NULL;
    }

    // find corresponding iteration data
    res = blitz_normalize_path(&tpl->tmp_buf, path, path_len, tpl->current_path, current_len TSRMLS_CC);
    current_len = strlen(tpl->current_path);
    norm_len = strlen(tpl->tmp_buf);

    // 2DO: using something like current_iteration and current_iteration_parent can speed up next step, 
    // but for other speed-up purposes it's not guaranteed that current_iteration and current_iteration_parent 
    // point to really current values. that's why we cannot just check tpl->current_path == tpl->tmp_buf and use them
    // we always find iteration by path instead
    res = blitz_find_iteration_by_path(tpl, tpl->tmp_buf, norm_len, &path_iteration, &path_iteration_parent TSRMLS_CC);
    if (!res) {
        if (BLITZ_DEBUG) php_printf("blitz_find_iteration_by_path failed!\n");
        final_params = input_arr;
    } else {
        if (BLITZ_DEBUG) php_printf("blitz_find_iteration_by_path: pi=%p ti=%p &ti=%p\n", path_iteration, tpl->iterations, &tpl->iterations);
        final_params = path_iteration;
    }

    if (BLITZ_DEBUG) {
        php_printf("tpl->iterations:\n");
        if (tpl->iterations && &tpl->iterations) php_var_dump(&tpl->iterations,1 TSRMLS_CC);
    }

    // merge data with input params
    if (input_arr && path_iteration) {
        if (BLITZ_DEBUG) php_printf("merging current iteration and params in fetch\n");
        input_ht = Z_ARRVAL_P(input_arr);
        zend_hash_internal_pointer_reset(Z_ARRVAL_P(path_iteration));
        zend_hash_internal_pointer_reset(input_ht);

        while (zend_hash_get_current_data(input_ht, (void**) &elem) == SUCCESS) {
            if (zend_hash_get_current_key_ex(input_ht, &key, &key_len, &index, 0, NULL) != HASH_KEY_IS_STRING) {
                zend_hash_move_forward(input_ht);
                continue;
            }

            if (key) {
                zval *temp;
                ALLOC_INIT_ZVAL(temp);
                *temp = **elem;
                zval_copy_ctor(temp);

                res = zend_hash_update(
                    Z_ARRVAL_P(path_iteration),
                    key,
                    key_len,
                    (void*)&temp, sizeof(zval *), NULL
                );
            }
            zend_hash_move_forward(input_ht);
        }
    }

    if (BLITZ_DEBUG) {
        php_printf("tpl->iterations:\n");
        if (tpl->iterations && &tpl->iterations) php_var_dump(&tpl->iterations,1 TSRMLS_CC);
        php_printf("final_params:\n");
        if (final_params && &final_params) php_var_dump(&final_params,1 TSRMLS_CC);
    }

    exec_status = blitz_fetch_node_by_path(tpl, id, 
        tpl->tmp_buf, norm_len, final_params, &result, &result_len TSRMLS_CC);
    
    if (exec_status) {
        ZVAL_STRINGL(return_value,result,result_len,1);
        if (exec_status == 1) efree(result);

        // clean-up parent path iteration after the fetch
        if (path_iteration_parent) {
            zend_hash_internal_pointer_end(Z_ARRVAL_P(path_iteration_parent));
            if (HASH_KEY_IS_LONG == zend_hash_get_current_key_ex(Z_ARRVAL_P(path_iteration_parent), &key, &key_len, &index, 0, NULL)) {
                //tpl->last_iteration = NULL;
                zend_hash_index_del(Z_ARRVAL_P(path_iteration_parent),index);
            } else {
                zend_hash_clean(Z_ARRVAL_P(path_iteration_parent));
            }
        } 

        // reset current iteration pointer if it's current iteration
        if ((current_len == norm_len) && (0 == strncmp(tpl->tmp_buf, tpl->current_path, norm_len))) {
            tpl->current_iteration = NULL;
        }

    } else {
        RETURN_FALSE;
    }

    return;
}
/* }}} */

/* {{{ blitz_clean(path, warn_not_found) */
/**********************************************************************************************************************/
PHP_FUNCTION(blitz_clean) {
/**********************************************************************************************************************/
    zval *id = NULL;
    zval **desc = NULL;
    blitz_tpl *tpl;
    char *path = NULL;
    int path_len = 0, norm_len = 0, res = 0, current_len = 0;
    char *key = NULL;
    int key_len = 0;
    long index = 0;
    zval *path_iteration = NULL;
    zval *path_iteration_parent = NULL;
    zval *warn_param = NULL;
    int warn_not_found = 1;

    BLITZ_FETCH_TPL_RESOURCE(id, tpl, desc);

    if (FAILURE == zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,"|sz",&path,&path_len,&warn_param)) {
        WRONG_PARAM_COUNT;
        RETURN_FALSE;
    }

    // warn_param = FALSE: throw no warning when iteration is not found. All other cases - do please.
    if (warn_param && (IS_BOOL == Z_TYPE_P(warn_param)) && (0 == Z_LVAL_P(warn_param))) {
        warn_not_found = 0;
    }

    // find corresponding iteration data
    res = blitz_normalize_path(&tpl->tmp_buf, path, path_len, tpl->current_path, current_len TSRMLS_CC);

    current_len = strlen(tpl->current_path);
    norm_len = strlen(tpl->tmp_buf);

    res = blitz_find_iteration_by_path(tpl, tpl->tmp_buf, norm_len, &path_iteration, &path_iteration_parent TSRMLS_CC);
    if (!res) {
        if (warn_not_found) {
            php_error(E_WARNING, "unable to find iteration by path %s", tpl->tmp_buf);
            RETURN_FALSE;
        } else {
            RETURN_TRUE;
        }
    }

    // clean-up parent iteration
    zend_hash_clean(Z_ARRVAL_P(path_iteration_parent));

    // reset current iteration pointer if it's current iteration 
    if ((current_len == norm_len) && (0 == strncmp(tpl->tmp_buf, tpl->current_path, norm_len))) {
        tpl->current_iteration = NULL;
    }

    RETURN_TRUE;
}
/* }}} */

/*
    2DO: useful extensions: gettext, q, qq 
    PHP_GETTEXT_H
*/

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */