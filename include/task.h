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

#ifndef CONCURRENT_TASK_H
#define CONCURRENT_TASK_H

#include "php.h"
#include "awaitable.h"

typedef void* concurrent_fiber_context;
typedef struct _concurrent_context concurrent_context;
typedef struct _concurrent_task_scheduler concurrent_task_scheduler;

BEGIN_EXTERN_C()

extern zend_class_entry *concurrent_task_ce;

typedef struct _concurrent_task concurrent_task;

struct _concurrent_task {
	/* Embedded fiber. */
	concurrent_fiber fiber;

	/* Task scheduler being used to execute the task. */
	concurrent_task_scheduler *scheduler;

	/* Unique identifier of this task. */
	size_t id;

	/* Async execution context provided to the task. */
	concurrent_context *context;

	/* Next task scheduled for execution. */
	concurrent_task *next;

	/* Next operation to be performed by the scheduler, one of the CONCURRENT_TASK_OPERATION_* constants. */
	zend_uchar operation;

	/* Error to be thrown into a task, must be set to UNDEF to resume tasks with a value. */
	zval error;

	/* Return value of the task, may also be an error object, check status for outcome. */
	zval result;

	/* Linked list of registered continuation callbacks. */
	concurrent_awaitable_cb *continuation;
};

extern const zend_uchar CONCURRENT_FIBER_TYPE_TASK;

extern const zend_uchar CONCURRENT_TASK_OPERATION_NONE;
extern const zend_uchar CONCURRENT_TASK_OPERATION_START;
extern const zend_uchar CONCURRENT_TASK_OPERATION_RESUME;

concurrent_task *concurrent_task_object_create();

void concurrent_task_start(concurrent_task *task);
void concurrent_task_continue(concurrent_task *task);

void concurrent_task_ce_register();

END_EXTERN_C()

typedef struct _concurrent_task_stop_info {
	concurrent_task_scheduler *scheduler;
	zend_bool required;
} concurrent_task_stop_info;

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
