/*
  +----------------------------------------------------------------------+
  | parallel                                                              |
  +----------------------------------------------------------------------+
  | Copyright (c) Joe Watkins 2019                                       |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: krakjoe                                                      |
  +----------------------------------------------------------------------+
 */
#ifndef HAVE_PARALLEL_H
#define HAVE_PARALLEL_H

#include "SAPI.h"
#include "php_main.h"
#include "zend_closures.h"
#include "zend_interfaces.h"
#include "zend_exceptions.h"
#include "zend_vm.h"

#include "copy.h"

#define php_parallel_exception_ex(type, m, ...) zend_throw_error(NULL, m, ##__VA_ARGS__)

extern zend_class_entry *php_parallel_runtime_error_illegal_function_ce;
extern zend_class_entry *php_parallel_runtime_error_illegal_instruction_ce;
extern zend_class_entry *php_parallel_runtime_error_illegal_parameter_ce;
extern zend_class_entry *php_parallel_runtime_error_illegal_return_ce;

#endif
