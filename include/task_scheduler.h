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

#ifndef CONCURRENT_TASK_SCHEDULER_H
#define CONCURRENT_TASK_SCHEDULER_H

#include "php.h"

typedef struct _concurrent_task concurrent_task;
typedef struct _concurrent_context concurrent_context;

BEGIN_EXTERN_C()

extern zend_class_entry *concurrent_task_scheduler_ce;

typedef struct _concurrent_task_scheduler concurrent_task_scheduler;

struct _concurrent_task_scheduler {
	/* Task PHP object handle. */
	zend_object std;

	/* Number of tasks scheduled to run. */
	size_t scheduled;

	/* Points to the next task to be run. */
	concurrent_task *first;

	/* Points to the last task to be run (needed to insert tasks into the run queue. */
	concurrent_task *last;

	concurrent_context *context;

	zend_bool running;
	zend_bool activate;

	zend_bool activator;
	zend_fcall_info activator_fci;
	zend_fcall_info_cache activator_fcc;

	zend_bool adapter;
	zend_fcall_info adapter_fci;
	zend_fcall_info_cache adapter_fcc;
};

zend_bool concurrent_task_scheduler_enqueue(concurrent_task *task);

void concurrent_task_scheduler_ce_register();

END_EXTERN_C()

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
