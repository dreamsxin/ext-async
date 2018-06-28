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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ucontext.h>

#include "php.h"
#include "zend.h"

#include "fiber.h"
#include "fiber_stack.h"

typedef struct _concurrent_fiber_context_ucontext concurrent_fiber_context_ucontext;

struct _concurrent_fiber_context_ucontext {
	ucontext_t ctx;
	concurrent_fiber_stack stack;
	concurrent_fiber_context_ucontext *caller;
	zend_bool initialized;
	zend_bool root;
};

char *concurrent_fiber_backend_info()
{
	return "ucontext (POSIX.1-2001, deprecated since POSIX.1-2004)";
}

concurrent_fiber_context concurrent_fiber_create_root_context()
{
	concurrent_fiber_context_ucontext *context;

	context = emalloc(sizeof(concurrent_fiber_context_ucontext));
	ZEND_SECURE_ZERO(context, sizeof(concurrent_fiber_context_ucontext));

	context->initialized = 1;
	context->root = 1;

	return (concurrent_fiber_context) context;
}

concurrent_fiber_context concurrent_fiber_create_context()
{
	concurrent_fiber_context_ucontext *context;

	context = emalloc(sizeof(concurrent_fiber_context_ucontext));
	ZEND_SECURE_ZERO(context, sizeof(concurrent_fiber_context_ucontext));

	return (concurrent_fiber_context) context;
}

zend_bool concurrent_fiber_create(concurrent_fiber_context ctx, concurrent_fiber_func func, size_t stack_size)
{
	concurrent_fiber_context_ucontext *context;

	context = (concurrent_fiber_context_ucontext *) ctx;

	if (UNEXPECTED(context->initialized == 1)) {
		return 0;
	}

	if (!concurrent_fiber_stack_allocate(&context->stack, stack_size)) {
		return 0;
	}

	if (getcontext(&context->ctx) == -1) {
		return 0;
	}

	context->ctx.uc_link = 0;
	context->ctx.uc_stack.ss_sp = context->stack.pointer;
	context->ctx.uc_stack.ss_size = context->stack.size;
	context->ctx.uc_stack.ss_flags = 0;

	makecontext(&context->ctx, func, 0);

	context->initialized = 1;

	return 1;
}

void concurrent_fiber_destroy(concurrent_fiber_context ctx)
{
	concurrent_fiber_context_ucontext *context;

	context = (concurrent_fiber_context_ucontext *) ctx;

	if (context != NULL) {
		if (!context->root && context->initialized) {
			concurrent_fiber_stack_free(&context->stack);
		}

		efree(context);
		context = NULL;
	}
}

zend_bool concurrent_fiber_switch_context(concurrent_fiber_context current, concurrent_fiber_context next)
{
	concurrent_fiber_context_ucontext *from;
	concurrent_fiber_context_ucontext *to;

	if (UNEXPECTED(current == NULL) || UNEXPECTED(next == NULL)) {
		return 0;
	}

	from = (concurrent_fiber_context_ucontext *) current;
	to = (concurrent_fiber_context_ucontext *) next;

	if (UNEXPECTED(from->initialized == 0) || UNEXPECTED(to->initialized == 0)) {
		return 0;
	}

	to->caller = from;

	if (swapcontext(&from->ctx, &to->ctx) == -1) {
		return 0;
	}

	return 1;
}

zend_bool concurrent_fiber_yield(concurrent_fiber_context current)
{
	concurrent_fiber_context_ucontext *fiber;

	if (UNEXPECTED(current == NULL)) {
		return 0;
	}

	fiber = (concurrent_fiber_context_ucontext *) current;

	if (UNEXPECTED(fiber->initialized == 0)) {
		return 0;
	}

	if (swapcontext(&fiber->ctx, &fiber->caller->ctx) == -1) {
		return 0;
	}

	return 1;
}

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
