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
#include "fiber_stack.h"

#undef ASM_CALLDECL
#if (defined(i386) || defined(__i386__) || defined(__i386) \
     || defined(__i486__) || defined(__i586__) || defined(__i686__) \
     || defined(__X86__) || defined(_X86_) || defined(__THW_INTEL__) \
     || defined(__I86__) || defined(__INTEL__) || defined(__IA32__) \
     || defined(_M_IX86) || defined(_I86_)) && defined(ZEND_WIN32)
#define ASM_CALLDECL __cdecl
#else
#define ASM_CALLDECL
#endif

typedef void* fcontext_t;

typedef struct _transfer_t {
    fcontext_t ctx;
    void *data;
} transfer_t;

extern fcontext_t ASM_CALLDECL make_fcontext(void *sp, size_t size, void (*fn)(transfer_t));
extern transfer_t ASM_CALLDECL jump_fcontext(fcontext_t to, void *vp);

typedef struct _async_fiber_context_asm {
	fcontext_t ctx;
	fcontext_t caller;
	async_fiber_stack stack;
	zend_bool initialized;
	zend_bool root;
} async_fiber_context_asm;

typedef struct _async_fiber_record_asm {
	async_fiber_func func;
} async_fiber_record_asm;

char *async_fiber_backend_info()
{
	return "asm (boost.context v1.67.0)";
}

static void async_fiber_asm_start(transfer_t trans)
{
	async_fiber_record_asm *record;
	async_fiber_context_asm *context;

	record = (async_fiber_record_asm *) trans.data;

	trans = jump_fcontext(trans.ctx, 0);
	context = (async_fiber_context_asm *) trans.data;

	if (context != NULL) {
		context->caller = trans.ctx;
	}

	record->func();
}

async_fiber_context async_fiber_create_root_context()
{
	async_fiber_context_asm *context;

	context = emalloc(sizeof(async_fiber_context_asm));
	ZEND_SECURE_ZERO(context, sizeof(async_fiber_context_asm));

	context->initialized = 1;
	context->root = 1;

	return (async_fiber_context) context;
}

async_fiber_context async_fiber_create_context()
{
	async_fiber_context_asm *context;

	context = emalloc(sizeof(async_fiber_context_asm));
	ZEND_SECURE_ZERO(context, sizeof(async_fiber_context_asm));

	return (async_fiber_context) context;
}

zend_bool async_fiber_create(async_fiber_context ctx, async_fiber_func func, size_t stack_size)
{
	static __thread size_t record_size;

	async_fiber_context_asm *context;
	async_fiber_record_asm *record;

	context = (async_fiber_context_asm *) ctx;

	if (UNEXPECTED(context->initialized == 1)) {
		return 0;
	}

	if (!async_fiber_stack_allocate(&context->stack, stack_size)) {
		return 0;
	}

	if (!record_size) {
		record_size = (size_t) ceil((double) sizeof(async_fiber_record_asm) / 64) * 64;
	}

	void *sp = (void *) (context->stack.size - record_size + (char *) context->stack.pointer);

	record = (async_fiber_record_asm *) sp;
	record->func = func;

	sp -= 64;

	context->ctx = make_fcontext(sp, sp - (void *) context->stack.pointer, &async_fiber_asm_start);
	context->ctx = jump_fcontext(context->ctx, record).ctx;

	context->initialized = 1;

	return 1;
}

void async_fiber_destroy(async_fiber_context ctx)
{
	async_fiber_context_asm *context;

	context = (async_fiber_context_asm *) ctx;

	if (context != NULL) {
		if (!context->root && context->initialized) {
			async_fiber_stack_free(&context->stack);
		}

		efree(context);
		context = NULL;
	}
}

zend_bool async_fiber_switch_context(async_fiber_context current, async_fiber_context next)
{
	async_fiber_context_asm *from;
	async_fiber_context_asm *to;

	if (UNEXPECTED(current == NULL) || UNEXPECTED(next == NULL)) {
		return 0;
	}

	from = (async_fiber_context_asm *) current;
	to = (async_fiber_context_asm *) next;

	if (UNEXPECTED(from->initialized == 0) || UNEXPECTED(to->initialized == 0)) {
		return 0;
	}

	to->ctx = jump_fcontext(to->ctx, to).ctx;

	return 1;
}

zend_bool async_fiber_yield(async_fiber_context current)
{
	async_fiber_context_asm *fiber;

	if (UNEXPECTED(current == NULL)) {
		return 0;
	}

	fiber = (async_fiber_context_asm *) current;

	if (UNEXPECTED(fiber->initialized == 0)) {
		return 0;
	}

	fiber->caller = jump_fcontext(fiber->caller, 0).ctx;

	return 1;
}

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
