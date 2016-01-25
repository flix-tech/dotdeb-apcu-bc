/*
  +----------------------------------------------------------------------+
  | APC                                                                  |
  +----------------------------------------------------------------------+
  | Copyright (c) 2015 The PHP Group     	                             |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: krakjoe													 |
  +----------------------------------------------------------------------+
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "php.h"
#include "zend.h"
#include "zend_API.h"
#include "zend_compile.h"
#include "zend_hash.h"
#include "zend_extensions.h"
#include "zend_interfaces.h"

#include "php_apc.h"
#include "ext/standard/info.h"
#include "ext/apcu/php_apc.h"
#include "ext/apcu/apc_arginfo.h"
#include "ext/apcu/apc_iterator.h"

#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif

zend_class_entry *apc_bc_iterator_ce;
zend_object_handlers apc_bc_iterator_handlers;

/* {{{ arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_apcu_bc_cache_info, 0, 0, 0)
    ZEND_ARG_INFO(0, type)
    ZEND_ARG_INFO(0, limited)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_apcu_bc_clear_cache, 0, 0, 0)
    ZEND_ARG_INFO(0, type)
ZEND_END_ARG_INFO()
/* }}} */

/* {{{ PHP_FUNCTION declarations */
PHP_FUNCTION(apc_cache_info);
PHP_FUNCTION(apc_clear_cache);
/* }}} */

/* {{{ PHP_MINFO_FUNCTION(apc) */
static PHP_MINFO_FUNCTION(apc)
{
    php_info_print_table_start();
    php_info_print_table_row(2, "APC Compatibility", PHP_APCU_BC_VERSION);
    php_info_print_table_row(2, "APCu Version", PHP_APCU_VERSION);
    php_info_print_table_row(2, "Build Date", __DATE__ " " __TIME__);
    php_info_print_table_end();
}
/* }}} */

static int apc_bc_iterator_init(int module_number);

/* {{{ PHP_RINIT_FUNCTION(apc) */
static PHP_RINIT_FUNCTION(apc)
{
#if defined(ZTS) && defined(COMPILE_DL_APC)
        ZEND_TSRMLS_CACHE_UPDATE();
#endif

    return SUCCESS;
}
/* }}} */

/* {{{ proto void apc_clear_cache(string cache) */
PHP_FUNCTION(apc_clear_cache) { 
	zend_string *name = NULL;
	zval proxy;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "|S", &name) != SUCCESS) {
		return;
	}
	
	if (name && !strcasecmp(ZSTR_VAL(name), "user")) {
		ZVAL_STR(&proxy,
			zend_string_init(ZEND_STRL("apcu_clear_cache"), 0));
		call_user_function(EG(function_table), NULL, &proxy, return_value, 0, NULL);
		zval_ptr_dtor(&proxy);
	}
}
/* }}} */

/* {{{ proto array apc_cache_info(string cache [, bool limited = false]) */
PHP_FUNCTION(apc_cache_info) {
	zend_string *name = NULL;
	zval  param, *limited = &param, proxy;

	ZVAL_FALSE(&param);

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "|Sz", &name, &limited) != SUCCESS) {
		return;
	}

	if (name && !strcasecmp(ZSTR_VAL(name), "user")) {
		ZVAL_STR(&proxy,
			zend_string_init(ZEND_STRL("apcu_cache_info"), 0));
		call_user_function(EG(function_table), NULL, &proxy, return_value, 1, limited);
		zval_ptr_dtor(&proxy);
	}
}
/* }}} */

/* {{{ apc_functions[] */
zend_function_entry apc_functions[] = {
	PHP_FE(apc_cache_info,         arginfo_apcu_bc_cache_info)
	PHP_FE(apc_clear_cache,        arginfo_apcu_bc_clear_cache)
	PHP_FALIAS(apc_store,    apcu_store,    arginfo_apcu_store)
	PHP_FALIAS(apc_fetch,    apcu_fetch,    arginfo_apcu_fetch)
	PHP_FALIAS(apc_enabled,  apcu_enabled,  arginfo_apcu_enabled)
	PHP_FALIAS(apc_delete,   apcu_delete,   arginfo_apcu_delete)
	PHP_FALIAS(apc_add,      apcu_add,      arginfo_apcu_store)
	PHP_FALIAS(apc_sma_info, apcu_sma_info, arginfo_apcu_sma_info)
	PHP_FALIAS(apc_inc,      apcu_inc,      arginfo_apcu_inc)
	PHP_FALIAS(apc_dec,      apcu_dec,      arginfo_apcu_inc)
	PHP_FALIAS(apc_cas,      apcu_cas,      arginfo_apcu_cas)
	PHP_FALIAS(apc_exists,   apcu_exists,   arginfo_apcu_exists)
	PHP_FE_END
};
/* }}} */

/* {{{ arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_apc_iterator___construct, 0, 0, 1)
	ZEND_ARG_INFO(0, ignored)
	ZEND_ARG_INFO(0, search)
	ZEND_ARG_INFO(0, format)
	ZEND_ARG_INFO(0, chunk_size)
	ZEND_ARG_INFO(0, list)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_apc_iterator_void, 0, 0, 0)
ZEND_END_ARG_INFO()
/* }}} */

/* {{{ proto object APCIterator::__construct(cache, [ mixed search [, long format [, long chunk_size [, long list ]]]]) */
PHP_METHOD(apc_bc_iterator, __construct) {
	apc_iterator_t *iterator = apc_iterator_fetch(getThis());
	zend_long format = APC_ITER_ALL;
	zend_long chunk_size=0;
	zval *search = NULL;
	zend_long list = APC_LIST_ACTIVE;
	zend_string *cache;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "S|zlll", &cache, &search, &format, &chunk_size, &list) == FAILURE) {
		return;
	}

	if (apc_is_enabled()) {
		apc_iterator_obj_init(iterator, search, format, chunk_size, list);
	}
}
/* }}} */

/* {{{ apc_iterator_functions */
static zend_function_entry apc_iterator_functions[] = {
    PHP_ME(apc_bc_iterator,  __construct, arginfo_apc_iterator___construct, ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
    PHP_FE_END
};
/* }}} */

/* {{{ apc_bc_iterator_init */
static int apc_bc_iterator_init(int module_number) {
    zend_class_entry ce;

    INIT_CLASS_ENTRY(ce, "APCIterator", apc_iterator_functions);
    apc_bc_iterator_ce = zend_register_internal_class_ex(&ce, apc_iterator_get_ce());

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION(apc) */
static PHP_MINIT_FUNCTION(apc)
{
	if (apc_iterator_get_ce()) {
		apc_bc_iterator_init(module_number);
	}
	return SUCCESS;
}

static const zend_module_dep apc_deps[] = {
	ZEND_MOD_REQUIRED("apcu")
	ZEND_MOD_END
};

/* {{{ module definition structure */
zend_module_entry apc_module_entry = {
	STANDARD_MODULE_HEADER_EX,
	NULL,
	apc_deps,
    PHP_APC_EXTNAME,
    apc_functions,
    PHP_MINIT(apc),
    NULL,
    PHP_RINIT(apc),
    NULL,
    PHP_MINFO(apc),
    PHP_APCU_VERSION,
    STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_APC
ZEND_GET_MODULE(apc)
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE();
#endif
#endif
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim>600: expandtab sw=4 ts=4 sts=4 fdm=marker
 * vim<600: expandtab sw=4 ts=4 sts=4
 */
