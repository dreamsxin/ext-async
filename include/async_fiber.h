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

#ifndef ASYNC_FIBER_H
#define ASYNC_FIBER_H

#define ASYNC_FIBER_VM_STACK_SIZE 4096

#ifdef PHP_WIN32
#define ASYNC_FIBER_CALLBACK static VOID __stdcall
typedef LPFIBER_START_ROUTINE async_fiber_cb;
#else
#define ASYNC_FIBER_CALLBACK static void
typedef void (* async_fiber_cb)(void *arg);
#endif

typedef enum {
	ASYNC_FIBER_SUSPEND_NONE,
	ASYNC_FIBER_SUSPEND_PREPEND,
	ASYNC_FIBER_SUSPEND_APPEND
} async_fiber_suspend_type;

const char *async_fiber_backend_info();

async_fiber *async_fiber_create_root();
async_fiber *async_fiber_create();
zend_bool async_fiber_init(async_fiber *fiber, async_context *context, async_fiber_cb func, void *arg, size_t stack_size);
void async_fiber_destroy(async_fiber *fiber);

void async_fiber_suspend(async_task_scheduler *scheduler);
void async_fiber_switch(async_task_scheduler *scheduler, async_fiber *to, async_fiber_suspend_type suspend);

#define async_fiber_copy_og(to, from) memcpy(to, from, sizeof(zend_output_globals));

static zend_always_inline void async_fiber_capture_og(async_context *context)
{
	if (UNEXPECTED(context->output.context->output.handler == NULL)) {
		context->output.context->output.handler = emalloc(sizeof(zend_output_globals));
	}
	
	async_fiber_copy_og(context->output.context->output.handler, &OG(handlers));
}

static zend_always_inline void async_fiber_restore_og(async_context *context)
{
	if (UNEXPECTED(context->output.context->output.handler == NULL)) {
		php_output_activate();
	} else {
		async_fiber_copy_og(&OG(handlers), context->output.context->output.handler);
	}
}

static zend_always_inline void async_fiber_capture_state(async_fiber *fiber)
{
	fiber->scheduler = ASYNC_G(scheduler);
	fiber->context = ASYNC_G(context);
	fiber->task = ASYNC_G(task);
	
	fiber->stack = EG(vm_stack);
	fiber->stack->top = EG(vm_stack_top);
	fiber->stack->end = EG(vm_stack_end);
	fiber->stack_page_size = EG(vm_stack_page_size);
	fiber->exec = EG(current_execute_data);
	fiber->exception_class = EG(exception_class);
	fiber->error_handling = EG(error_handling);
	fiber->bailout = EG(bailout);
	
	async_fiber_capture_og(fiber->context);
}

static zend_always_inline void async_fiber_restore_state(async_fiber *fiber)
{
	ASYNC_G(scheduler) = fiber->scheduler;
	ASYNC_G(context) = fiber->context;
	ASYNC_G(task) = fiber->task;
	
	EG(vm_stack) = fiber->stack;
	EG(vm_stack_top) = fiber->stack->top;
	EG(vm_stack_end) = fiber->stack->end;
	EG(vm_stack_page_size) = fiber->stack_page_size;
	EG(current_execute_data) = fiber->exec;
	EG(exception_class) = fiber->exception_class;
	EG(error_handling) = fiber->error_handling;
	EG(bailout) = fiber->bailout;
	
	async_fiber_restore_og(fiber->context);
}

#endif
