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


async_awaitable_cb *async_awaitable_register_continuation(async_awaitable_queue *q, void *obj, zval *data, async_awaitable_func func)
{
	async_awaitable_cb *cb;

	cb = emalloc(sizeof(async_awaitable_cb));
	ZEND_SECURE_ZERO(cb, sizeof(async_awaitable_cb));

	cb->object = obj;
	cb->func = func;

	if (data == NULL) {
		ZVAL_UNDEF(&cb->data);
	} else {
		ZVAL_COPY(&cb->data, data);
	}

	ASYNC_Q_ENQUEUE(q, cb);

	return cb;
}

void async_awaitable_dispose_continuation(async_awaitable_queue *q, async_awaitable_cb *cb)
{
	ASYNC_Q_DETACH(q, cb);

	zval_ptr_dtor(&cb->data);

	efree(cb);
}

void async_awaitable_trigger_continuation(async_awaitable_queue *q, zval *result, zend_bool success)
{
	async_awaitable_cb *cb;

	while (q->first != NULL) {
		ASYNC_Q_DEQUEUE(q, cb);

		cb->func(cb->object, &cb->data, result, success);
		zval_ptr_dtor(&cb->data);

		efree(cb);
	}
}

static int async_awaitable_implement_interface(zend_class_entry *entry, zend_class_entry *implementor)
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
		ZSTR_VAL(entry->name),
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
