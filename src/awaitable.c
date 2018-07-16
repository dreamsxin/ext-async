/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2018 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Martin Schröder <m.schroeder2007@gmail.com>                 |
  +----------------------------------------------------------------------+
*/

#include "php_async.h"

zend_class_entry *async_awaitable_ce;


void async_awaitable_register_continuation(async_awaitable_cb **cont, void *obj, zval *data, async_awaitable_func func)
{
	async_awaitable_cb *current;

	current = emalloc(sizeof(async_awaitable_cb));

	current->object = obj;
	current->func = func;
	current->next = NULL;

	if (data == NULL) {
		ZVAL_UNDEF(&current->data);
	} else {
		ZVAL_COPY(&current->data, data);
	}

	if (*cont == NULL) {
		*cont = current;
	} else {
		(*cont)->next = current;
	}
}

void async_awaitable_trigger_continuation(async_awaitable_cb **cont, zval *result, zend_bool success)
{
	async_awaitable_cb *current;
	async_awaitable_cb *next;

	current = *cont;
	*cont = NULL;

	if (current != NULL) {
		do {
			next = current->next;
			*cont = next;

			current->func(current->object, &current->data, result, success);

			zval_ptr_dtor(&current->data);

			efree(current);

			current = next;
		} while (current != NULL);
	}
}

static int async_awaitable_implement_interface(zend_class_entry *interface, zend_class_entry *implementor)
{
	if (implementor == async_deferred_awaitable_ce) {
		return SUCCESS;
	}

	if (implementor == async_task_ce) {
		return SUCCESS;
	}

	zend_error_noreturn(
		E_CORE_ERROR,
		"Class %s must not implement interface %s, create an awaitable using %s instead",
		ZSTR_VAL(implementor->name),
		ZSTR_VAL(interface->name),
		ZSTR_VAL(async_deferred_ce->name)
	);

	return FAILURE;
}

static const zend_function_entry awaitable_functions[] = {
	ZEND_FE_END
};


void async_awaitable_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Awaitable", awaitable_functions);
	async_awaitable_ce = zend_register_internal_interface(&ce);
	async_awaitable_ce->interface_gets_implemented = async_awaitable_implement_interface;
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
