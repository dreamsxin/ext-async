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

char *async_fiber_backend_info();

void async_fiber_run();
void async_fiber_init_metadata(async_fiber *fiber, zend_execute_data *call);

async_fiber_context async_fiber_create_root_context();
async_fiber_context async_fiber_create_context();
zend_bool async_fiber_create(async_fiber_context context, async_fiber_func func, size_t stack_size);
void async_fiber_destroy(async_fiber_context context);

async_fiber_context async_fiber_context_get();
void async_fiber_context_start(async_fiber *fiber, async_context *context, zend_bool yieldable);
void async_fiber_context_switch(async_fiber_context *context, zend_bool yieldable);
void async_fiber_context_yield();

zend_bool async_fiber_switch_context(async_fiber_context current, async_fiber_context next, zend_bool yieldable);
zend_bool async_fiber_yield(async_fiber_context current);

#define ASYNC_FIBER_BACKUP_VM_STATE(state) do { \
	(state)->stack = EG(vm_stack); \
	(state)->stack->top = EG(vm_stack_top); \
	(state)->stack->end = EG(vm_stack_end); \
	(state)->stack_page_size = EG(vm_stack_page_size); \
	(state)->exec = EG(current_execute_data); \
	(state)->fake_scope = EG(fake_scope); \
	(state)->exception_class = EG(exception_class); \
	(state)->error_handling = EG(error_handling); \
} while (0)

#define ASYNC_FIBER_RESTORE_VM_STATE(state) do { \
	EG(vm_stack) = (state)->stack; \
	EG(vm_stack_top) = (state)->stack->top; \
	EG(vm_stack_end) = (state)->stack->end; \
	EG(vm_stack_page_size) = (state)->stack_page_size; \
	EG(current_execute_data) = (state)->exec; \
	EG(fake_scope) = (state)->fake_scope; \
	EG(exception_class) = (state)->exception_class; \
	EG(error_handling) = (state)->error_handling; \
} while (0)

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
