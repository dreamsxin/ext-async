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

zend_bool async_fiber_switch_context(async_fiber_context current, async_fiber_context next);
zend_bool async_fiber_switch_to(async_fiber *fiber);
zend_bool async_fiber_yield(async_fiber_context current);

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
