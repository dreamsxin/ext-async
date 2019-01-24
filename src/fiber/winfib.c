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

#include "async_fiber.h"

static int counter = 0;

typedef struct async_fiber_context_win32_ async_fiber_context_win32;

struct async_fiber_context_win32_ {
	void *fiber;
	async_fiber_context_win32 *caller;
	int id;
	zend_bool root;
	zend_bool initialized;
};

char *async_fiber_backend_info()
{
	return "winfib (Windows Fiber API)";
}

async_fiber_context async_fiber_create_root_context()
{
	async_fiber_context_win32 *context;

	context = emalloc(sizeof(async_fiber_context_win32));
	ZEND_SECURE_ZERO(context, sizeof(async_fiber_context_win32));

	context->root = 1;
	context->initialized = 1;

	if (IsThreadAFiber()) {
		context->fiber = GetCurrentFiber();
	} else {
		context->fiber = ConvertThreadToFiberEx(0, FIBER_FLAG_FLOAT_SWITCH);
	}

	if (context->fiber == NULL) {
		return NULL;
	}

	return (async_fiber_context)context;
}

async_fiber_context async_fiber_create_context()
{
	async_fiber_context_win32 *context;

	context = emalloc(sizeof(async_fiber_context_win32));
	ZEND_SECURE_ZERO(context, sizeof(async_fiber_context_win32));

	context->id = ++counter;

	return (async_fiber_context) context;
}

zend_bool async_fiber_create(async_fiber_context ctx, async_fiber_func func, size_t stack_size)
{
	async_fiber_context_win32 *context;

	context = (async_fiber_context_win32 *) ctx;

	if (UNEXPECTED(context->initialized == 1)) {
		return 0;
	}

	context->fiber = CreateFiberEx(stack_size, stack_size, FIBER_FLAG_FLOAT_SWITCH, (void (*)(void *))func, context);

	if (context->fiber == NULL) {
		return 0;
	}

	context->initialized = 1;

	return 1;
}

void async_fiber_destroy(async_fiber_context ctx)
{
	async_fiber_context_win32 *context;

	context = (async_fiber_context_win32 *) ctx;

	if (context != NULL) {
		if (context->root) {
			ConvertFiberToThread();
		} else if (context->initialized) {
			DeleteFiber(context->fiber);
		}

		efree(context);
		context = NULL;
	}
}

zend_bool async_fiber_switch_context(async_fiber_context current, async_fiber_context next, zend_bool yieldable)
{
	async_fiber_context_win32 *from;
	async_fiber_context_win32 *to;

	if (UNEXPECTED(current == NULL) || UNEXPECTED(next == NULL)) {
		return 0;
	}

	from = (async_fiber_context_win32 *) current;
	to = (async_fiber_context_win32 *) next;

	if (UNEXPECTED(from->initialized == 0) || UNEXPECTED(to->initialized == 0)) {
		return 0;
	}

	if (yieldable) {
		to->caller = from;
	}

	ASYNC_DEBUG_LOG("FIBER SWITCH: %d -> %d\n", from->id, to->id);

	SwitchToFiber(to->fiber);

	return 1;
}

zend_bool async_fiber_yield(async_fiber_context current)
{
	async_fiber_context_win32 *fiber;

	if (UNEXPECTED(current == NULL)) {
		return 0;
	}

	fiber = (async_fiber_context_win32 *) current;

	if (UNEXPECTED(fiber->initialized == 0)) {
		return 0;
	}

	ASYNC_DEBUG_LOG("FIBER YIELD: %d -> %d\n", fiber->id, fiber->caller->id);

	SwitchToFiber(fiber->caller->fiber);

	return 1;
}
