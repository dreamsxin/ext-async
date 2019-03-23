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

#define ASYNC_FIBER_WINFIB_FLAG_ROOT 1
#define ASYNC_FIBER_WINFIB_FLAG_INITIALIZED 2
#define ASYNC_FIBER_WINFIB_FLAG_WAS_FIBER 4

static int counter = 0;

typedef struct {
	async_fiber base;
	void *fiber;
	int id;
	uint8_t flags;
} async_fiber_win32;


const char *async_fiber_backend_info()
{
	return "winfib (Windows Fiber API)";
}

async_fiber *async_fiber_create_root()
{
	async_fiber_win32 *fiber;

	fiber = ecalloc(1, sizeof(async_fiber_win32));

	fiber->flags = ASYNC_FIBER_WINFIB_FLAG_ROOT | ASYNC_FIBER_WINFIB_FLAG_INITIALIZED;

	if (UNEXPECTED(IsThreadAFiber())) {
		fiber->fiber = GetCurrentFiber();
		fiber->flags |= ASYNC_FIBER_WINFIB_FLAG_WAS_FIBER;
	} else {
		fiber->fiber = ConvertThreadToFiberEx(0, FIBER_FLAG_FLOAT_SWITCH);
	}

	if (UNEXPECTED(fiber->fiber == NULL)) {
		return NULL;
	}

	return (async_fiber *) fiber;
}

async_fiber *async_fiber_create()
{
	async_fiber_win32 *fiber;

	fiber = ecalloc(1, sizeof(async_fiber_win32));

	fiber->id = ++counter;

	return (async_fiber *) fiber;
}

zend_bool async_fiber_init(async_fiber *fiber, async_context *context, async_fiber_cb func, void *arg, size_t stack_size)
{
	async_fiber_win32 *impl;

	impl = (async_fiber_win32 *) fiber;

	ZEND_ASSERT(!(impl->flags & ASYNC_FIBER_WINFIB_FLAG_INITIALIZED));

	impl->fiber = CreateFiberEx(stack_size, stack_size, FIBER_FLAG_FLOAT_SWITCH, func, (LPVOID) arg);

	if (UNEXPECTED(impl->fiber == NULL)) {
		return 0;
	}

	impl->flags |= ASYNC_FIBER_WINFIB_FLAG_INITIALIZED;
	
	fiber->context = context;

	return 1;
}

void async_fiber_destroy(async_fiber *fiber)
{
	async_fiber_win32 *impl;

	impl = (async_fiber_win32 *) fiber;

	if (EXPECTED(impl != NULL)) {
		if (UNEXPECTED(impl->flags & ASYNC_FIBER_WINFIB_FLAG_ROOT)) {
			if (!(impl->flags & ASYNC_FIBER_WINFIB_FLAG_WAS_FIBER)) {
				ConvertFiberToThread();
			}
		} else if (impl->flags & ASYNC_FIBER_WINFIB_FLAG_INITIALIZED) {
			DeleteFiber(impl->fiber);
		}

		efree(impl);
	}
}

void async_fiber_suspend(async_task_scheduler *scheduler)
{
	async_fiber *current;
	async_fiber *next;
	
	async_fiber_win32 *from;
	async_fiber_win32 *to;
	
	current = ASYNC_G(fiber);
	
	ZEND_ASSERT(scheduler != NULL);
	ZEND_ASSERT(current != NULL);
	
	if (scheduler->fibers.first == NULL) {
		next = scheduler->runner;
	} else {
		ASYNC_LIST_EXTRACT_FIRST(&scheduler->fibers, next);
		
		next->flags &= ~ASYNC_FIBER_FLAG_QUEUED;
	}
	
	if (UNEXPECTED(current == next)) {
		return;
	}

	from = (async_fiber_win32 *) current;
	to = (async_fiber_win32 *) next;
	
	ZEND_ASSERT(next != NULL);
	ZEND_ASSERT(from->flags & ASYNC_FIBER_WINFIB_FLAG_INITIALIZED);
	ZEND_ASSERT(to->flags & ASYNC_FIBER_WINFIB_FLAG_INITIALIZED);
	
	// ASYNC_DEBUG_LOG("SUSPEND: %d -> %d\n", from->id, to->id);
	
	async_fiber_capture_state(current);	
	ASYNC_G(fiber) = next;
	
	SwitchToFiber(to->fiber);
	
	async_fiber_restore_state(current);
}

void async_fiber_switch(async_task_scheduler *scheduler, async_fiber *next, async_fiber_suspend_type suspend)
{
	async_fiber *current;
	
	async_fiber_win32 *from;
	async_fiber_win32 *to;
	
	ZEND_ASSERT(scheduler != NULL);
	ZEND_ASSERT(next != NULL);
	
	current = ASYNC_G(fiber);
	
	if (current == NULL) {
		current = ASYNC_G(root);
	}
	
	ZEND_ASSERT(current != NULL);
	
	if (UNEXPECTED(current == next)) {
		return;
	}

	from = (async_fiber_win32 *) current;
	to = (async_fiber_win32 *) next;

	ZEND_ASSERT(from->flags & ASYNC_FIBER_WINFIB_FLAG_INITIALIZED);
	ZEND_ASSERT(to->flags & ASYNC_FIBER_WINFIB_FLAG_INITIALIZED);
	ZEND_ASSERT(current != next);
	
	if (EXPECTED(!(current->flags & ASYNC_FIBER_FLAG_QUEUED))) {
		switch (suspend) {
		case ASYNC_FIBER_SUSPEND_PREPEND:
			ASYNC_LIST_PREPEND(&scheduler->fibers, current);
			current->flags |= ASYNC_FIBER_FLAG_QUEUED;
			break;
		case ASYNC_FIBER_SUSPEND_APPEND:
			ASYNC_LIST_APPEND(&scheduler->fibers, current);
			current->flags |= ASYNC_FIBER_FLAG_QUEUED;
			break;
		case ASYNC_FIBER_SUSPEND_NONE:
			break;
		}
	}
	
	// ASYNC_DEBUG_LOG("SWITCH: %d -> %d\n", from->id, to->id);
	
	async_fiber_capture_state(current);	
	ASYNC_G(fiber) = next;
	
	SwitchToFiber(to->fiber);
	
	async_fiber_restore_state(current);
}
