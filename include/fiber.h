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

#include "php.h"

BEGIN_EXTERN_C()

extern zend_class_entry *async_fiber_ce;

void async_fiber_ce_register();
void async_fiber_ce_unregister();

void async_fiber_shutdown();

typedef void* async_fiber_context;
typedef struct _async_fiber async_fiber;

typedef void (* async_fiber_func)();
typedef void (* async_fiber_run_func)(async_fiber *fiber);

struct _async_fiber {
	/* Fiber PHP object handle. */
	zend_object std;

	/* Unique identifier of the fiber (only unique as long as the object exists!). */
	zend_string *id;

	/* Implementation-specific fiber type. */
	zend_uchar type;

	/* Status of the fiber, one of the ASYNC_FIBER_STATUS_* constants. */
	zend_uchar status;

	/* Flag indicating if the fiber has been disposed yet. */
	zend_bool disposed;

	/* Callback and info / cache to be used when fiber is started. */
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	/* Native fiber context of this fiber, will be created during call to start(). */
	async_fiber_context context;

	/* Custom fiber execution function (NULL for default handler). */
	async_fiber_run_func func;

	/* Destination for a PHP value being passed into or returned from the fiber. */
	zval *value;

	/* Current Zend VM execute data being run by the fiber. */
	zend_execute_data *exec;

	/* VM stack being used by the fiber. */
	zend_vm_stack stack;

	/* Max size of the C stack being used by the fiber. */
	size_t stack_size;
};

extern const zend_uchar ASYNC_FIBER_TYPE_DEFAULT;

extern const zend_uchar ASYNC_FIBER_STATUS_INIT;
extern const zend_uchar ASYNC_FIBER_STATUS_SUSPENDED;
extern const zend_uchar ASYNC_FIBER_STATUS_RUNNING;
extern const zend_uchar ASYNC_FIBER_STATUS_FINISHED;
extern const zend_uchar ASYNC_FIBER_STATUS_DEAD;

void async_fiber_run();
zend_bool async_fiber_switch_to(async_fiber *fiber);

char *async_fiber_backend_info();

async_fiber_context async_fiber_create_root_context();
async_fiber_context async_fiber_create_context();

zend_bool async_fiber_create(async_fiber_context context, async_fiber_func func, size_t stack_size);
void async_fiber_destroy(async_fiber_context context);

zend_bool async_fiber_switch_context(async_fiber_context current, async_fiber_context next);
zend_bool async_fiber_yield(async_fiber_context current);

#define ASYNC_FIBER_BACKUP_EG(stack, stack_page_size, exec) do { \
	stack = EG(vm_stack); \
	stack->top = EG(vm_stack_top); \
	stack->end = EG(vm_stack_end); \
	stack_page_size = EG(vm_stack_page_size); \
	exec = EG(current_execute_data); \
} while (0)

#define ASYNC_FIBER_RESTORE_EG(stack, stack_page_size, exec) do { \
	EG(vm_stack) = stack; \
	EG(vm_stack_top) = stack->top; \
	EG(vm_stack_end) = stack->end; \
	EG(vm_stack_page_size) = stack_page_size; \
	EG(current_execute_data) = exec; \
} while (0)

END_EXTERN_C()

#define REGISTER_FIBER_CLASS_CONST_LONG(const_name, value) \
	zend_declare_class_constant_long(async_fiber_ce, const_name, sizeof(const_name)-1, (zend_long)value);

#define ASYNC_FIBER_VM_STACK_SIZE 4096

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
