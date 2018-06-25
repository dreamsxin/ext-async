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

#ifndef TASK_H
#define TASK_H

#include "php.h"

#include "fiber.h"

BEGIN_EXTERN_C()

extern zend_class_entry *concurrent_awaitable_ce;

void concurrent_task_ce_register();
void concurrent_task_ce_unregister();

typedef struct _concurrent_task concurrent_task;
typedef struct _concurrent_task_scheduler concurrent_task_scheduler;

struct _concurrent_task {
	/* Task PHP object handle. */
	zend_object std;

	/* Status of the task, one of the ZEND_FIBER_STATUS_* constants. */
	zend_uchar status;

	/* Callback and info / cache to be used when task is started. */
	zend_fcall_info fci;
	zend_fcall_info_cache fci_cache;

	/* Native fiber context of this task, will be created during call to start(). */
	concurrent_fiber_context context;

	/* Destination for a PHP value being passed into or returned from the task. */
	zval *value;

	/* Current Zend VM execute data being run by the task. */
	zend_execute_data *exec;

	/* VM stack being used by the task. */
	zend_vm_stack stack;

	/* Max size of the C stack being used by the task. */
	size_t stack_size;

	/* Reference to the task scheduler being used to run the task. */
	concurrent_task_scheduler *scheduler;

	/* Next task scheduled for execution. */
	concurrent_task *next;

	/* Next operation to be performed by the scheduler. */
	zend_uchar operation;

	zend_bool await;
	zend_fcall_info awaiter;
	zend_fcall_info_cache awaiter_cache;
};

static const zend_uchar CONCURRENT_TASK_OPERATION_START = 0;
static const zend_uchar CONCURRENT_TASK_OPERATION_RESUME = 1;
static const zend_uchar CONCURRENT_TASK_OPERATION_ERROR = 2;

struct _concurrent_task_scheduler {
	/* Task PHP object handle. */
	zend_object std;

	size_t scheduled;

	concurrent_task *first;

	concurrent_task *last;
};

END_EXTERN_C()

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
