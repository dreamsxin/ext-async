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

#include "ext/standard/file.h"
#include "ext/standard/flock_compat.h"
#include "ext/standard/php_filestat.h"

#if HAVE_SYS_FILE_H
#include <sys/file.h>
#endif

#define ASYNC_STRIP_FILE_SCHEME(url) do { \
	if (strncasecmp(url, "file://", sizeof("file://") - 1) == 0) { \
		url += sizeof("file://") - 1; \
	} \
} while (0)

#define ASYNC_FS_CALL(data, req, func, ...) do { \
	async_uv_op *op; \
	int code; \
	op = NULL; \
	if ((data)->scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_DISPOSED) { \
		(data)->async = 0; \
	} \
	if ((data)->async) { \
		ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_uv_op)); \
		(req)->data = op;\
	} \
	code = (func)(&(data)->scheduler->loop, req, __VA_ARGS__, (data)->async ? dummy_cb : NULL); \
	if (code >= 0) { \
		if ((data)->async) { \
			if (async_await_op((async_op *) op) == FAILURE) { \
				ASYNC_FORWARD_OP_ERROR(op); \
				(req)->result = -1; \
				if (0 == uv_cancel((uv_req_t *) req)) { \
					ASYNC_FREE_OP(op); \
				} else { \
					((async_op *) op)->status = ASYNC_STATUS_FAILED; \
				} \
			} else { \
				code = op->code; \
				ASYNC_FREE_OP(op); \
			} \
		} \
	} else { \
		(req)->result = code; \
		if ((data)->async) { \
			ASYNC_FREE_OP(op); \
		} \
	} \
} while (0)

#define ASYNC_FS_CALLW(async, req, func, ...) do { \
	async_task_scheduler *scheduler; \
	async_uv_op *op; \
	int code; \
	op = NULL; \
	scheduler = async_task_scheduler_get(); \
	if (async) { \
		ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_uv_op)); \
		(req)->data = op; \
	} \
	code = (func)(&scheduler->loop, req, __VA_ARGS__, (async) ? dummy_cb : NULL); \
	if (code >= 0) { \
		if (async) { \
			if (async_await_op((async_op *) op) == FAILURE) { \
				ASYNC_FORWARD_OP_ERROR(op); \
				(req)->result = -1; \
				if (0 == uv_cancel((uv_req_t *) req)) { \
					ASYNC_FREE_OP(op); \
				} else { \
					((async_op *) op)->status = ASYNC_STATUS_FAILED; \
				} \
			} else { \
				code = op->code; \
				ASYNC_FREE_OP(op); \
			} \
		} \
	} else { \
		(req)->result = code; \
		if (async) { \
			ASYNC_FREE_OP(op); \
		} \
	} \
} while (0)

static php_stream_wrapper orig_file_wrapper;

typedef struct _async_dirstream_entry async_dirstream_entry;

struct _async_dirstream_entry {
	async_dirstream_entry *prev;
	async_dirstream_entry *next;

	/* Uses the struct hack to avoid additional memory allocations. */
	char name[1];
};

typedef struct {
	async_dirstream_entry *first;
	async_dirstream_entry *last;
	async_dirstream_entry *entry;
	zend_off_t offset;
	zend_off_t size;
	async_task_scheduler *scheduler;
} async_dirstream_data;

typedef struct {
	uv_file file;
	int mode;
	int lock_flag;
	zend_bool async;
	zend_bool finished;
	int64_t rpos;
	int64_t wpos;
	async_task_scheduler *scheduler;
} async_filestream_data;


static void dummy_cb(uv_fs_t* req)
{
	async_uv_op *op;
	
	op = (async_uv_op *) req->data;
	
	ZEND_ASSERT(op != NULL);
	
	if (req->result == UV_ECANCELED) {
		ASYNC_FREE_OP(op);
	} else {
		op->code = 0;

		ASYNC_FINISH_OP(op);
	}
}

static inline void map_stat(uv_stat_t *stat, php_stream_statbuf *ssb)
{
	memset(ssb, 0, sizeof(php_stream_statbuf));
	
	ssb->sb.st_dev = stat->st_dev;
	ssb->sb.st_mode = stat->st_mode;
	ssb->sb.st_size = stat->st_size;
	ssb->sb.st_nlink = stat->st_nlink;
	ssb->sb.st_rdev = stat->st_rdev;
	
#ifdef PHP_WIN32
	ssb->sb.st_atime = (time_t) stat->st_atim.tv_sec;
	ssb->sb.st_mtime = (time_t) stat->st_mtim.tv_sec;
	ssb->sb.st_ctime = (time_t) stat->st_ctim.tv_sec;
#else

#if defined(__APPLE__)
    ssb->sb.st_atimespec.tv_sec = (time_t) stat->st_atim.tv_sec;
	ssb->sb.st_atimespec.tv_nsec = stat->st_atim.tv_nsec;
	
	ssb->sb.st_mtimespec.tv_sec = (time_t) stat->st_mtim.tv_sec;
	ssb->sb.st_mtimespec.tv_nsec = stat->st_mtim.tv_nsec;
	
	ssb->sb.st_ctimespec.tv_sec = (time_t) stat->st_ctim.tv_sec;
	ssb->sb.st_ctimespec.tv_nsec = stat->st_ctim.tv_nsec;
#else
	ssb->sb.st_atim.tv_sec = (time_t) stat->st_atim.tv_sec;
	ssb->sb.st_atim.tv_nsec = stat->st_atim.tv_nsec;
	
	ssb->sb.st_mtim.tv_sec = (time_t) stat->st_mtim.tv_sec;
	ssb->sb.st_mtim.tv_nsec = stat->st_mtim.tv_nsec;
	
	ssb->sb.st_ctim.tv_sec = (time_t) stat->st_ctim.tv_sec;
	ssb->sb.st_ctim.tv_nsec = stat->st_ctim.tv_nsec;
#endif
	
	ssb->sb.st_ino = stat->st_ino;
	ssb->sb.st_uid = stat->st_uid;
	ssb->sb.st_gid = stat->st_gid;
	ssb->sb.st_blksize = stat->st_blksize;
	ssb->sb.st_blocks = stat->st_blocks;
#endif
}

static inline int parse_open_mode(const char *mode, int *mods)
{
	int flags;
	
	switch (mode[0]) {
	case 'r':
		flags = 0;
		break;
	case 'w':
		flags = UV_FS_O_TRUNC | UV_FS_O_CREAT;
		break;
	case 'a':
		flags = UV_FS_O_CREAT | UV_FS_O_APPEND;
		break;
	case 'x':
		flags = UV_FS_O_CREAT | UV_FS_O_EXCL;
		break;
	case 'c':
		flags = UV_FS_O_CREAT;
		break;
	default:
		return FAILURE;
	}
	
	if (strchr(mode, '+')) {
        flags |= UV_FS_O_RDWR;
    } else if (flags) {
        flags |= UV_FS_O_WRONLY;
    } else {
        flags |= UV_FS_O_RDONLY;
    }

#ifdef O_CLOEXEC
    if (strchr(mode, 'e')) {
        flags |= O_CLOEXEC;
    }
#endif

#ifdef O_NONBLOCK
    if (strchr(mode, 'n')) {
        flags |= O_NONBLOCK;
    }
#endif

#if defined(_O_TEXT) && defined(O_BINARY)
    if (strchr(mode, 't')) {
        flags |= _O_TEXT;
    } else {
        flags |= O_BINARY;
    }
#endif
	
	*mods = flags;
	
	return SUCCESS;
}


static size_t async_dirstream_read(php_stream *stream, char *buf, size_t count)
{
	async_dirstream_data *data;
	php_stream_dirent *ent;

	data = (async_dirstream_data *) stream->abstract;
	ent = (php_stream_dirent *) buf;
	
	if (data->entry == NULL) {
		return 0;
	}

	strcpy(ent->d_name, data->entry->name);

	data->entry = data->entry->next;
	data->offset++;

	return sizeof(php_stream_dirent);
}

static int async_dirstream_rewind(php_stream *stream, zend_off_t offset, int whence, zend_off_t *newoffs)
{
	async_dirstream_data *data;
	async_dirstream_entry *entry;

	zend_off_t i;

	data = (async_dirstream_data *) stream->abstract;

	switch (whence) {
	case SEEK_SET:
		entry = data->first;

		for (i = 0; i < offset; i++) {
			if (entry != NULL) {
				entry = entry->next;
			}
		}

		data->entry = entry;
		data->offset = offset;

		break;
	case SEEK_CUR:
		entry = data->first;

		for (i = 0; i < offset; i++) {
			if (entry != NULL) {
				entry = entry->next;
			}
		}

		data->entry = entry;
		data->offset += offset;

		break;
	case SEEK_END:
		entry = data->last;

		for (i = 0; i < offset; i++) {
			if (entry != NULL) {
				entry = entry->prev;
			}
		}

		data->entry = entry;
		data->offset = data->size - offset;

		break;
	default:
		return -1;
	}

	*newoffs = data->offset;

	return 0;
}

static int async_dirstream_close(php_stream *stream, int close_handle)
{
	async_dirstream_data *data;
	async_dirstream_entry *entry;
	async_dirstream_entry *prev;
	
	data = (async_dirstream_data *) stream->abstract;
	entry = data->first;
	
	while (entry != NULL) {
		prev = entry;
		entry = entry->next;

		efree(prev);
	}
	
	OBJ_RELEASE(&data->scheduler->std);
	
	efree(data);
	
	return 0;
}

static php_stream_ops async_dirstream_ops = {
	NULL,
	async_dirstream_read,
	async_dirstream_close,
	NULL,
	"dir/async",
	async_dirstream_rewind,
	NULL,
	NULL,
	NULL
};


static size_t async_filestream_write(php_stream *stream, const char *buf, size_t count)
{
	async_filestream_data *data;
	uv_fs_t req;
	uv_buf_t bufs[1];
	
	data = (async_filestream_data *) stream->abstract;
	
	if (data->scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_DISPOSED) {
		data->async = 0;
	}
	
	bufs[0] = uv_buf_init((char *) buf, count);
	
	ASYNC_FS_CALL(data, &req, uv_fs_write, data->file, bufs, 1, data->wpos);
	
	uv_fs_req_cleanup(&req);
	
	if (req.result < 0) {
		return 0;
	}
	
	data->wpos += req.result;

	return (size_t) req.result;
}

static size_t async_filestream_read(php_stream *stream, char *buf, size_t count)
{
	async_filestream_data *data;
	uv_fs_t req;
	uv_buf_t bufs[1];

	data = (async_filestream_data *) stream->abstract;
	
	if (data->finished || count < 8192) {
		return 0;
	}
	
	bufs[0] = uv_buf_init(buf, count);
	
	ASYNC_FS_CALL(data, &req, uv_fs_read, data->file, bufs, 1, data->rpos);
	
	uv_fs_req_cleanup(&req);
	
	if (req.result < 0) {
		return 0;
	}
	
	if (req.result < count) {
		data->finished = 1;
		stream->eof = 1;
	}
	
	data->rpos += req.result;

	return (size_t) req.result;
}

static int async_filestream_close(php_stream *stream, int close_handle)
{
	async_filestream_data *data;
	uv_fs_t req;
	
	data = (async_filestream_data *) stream->abstract;

	ASYNC_FS_CALL(data, &req, uv_fs_close, data->file);
	
	uv_fs_req_cleanup(&req);
		
	OBJ_RELEASE(&data->scheduler->std);
	
	efree(data);
	
	return (req.result < 1) ? 1 : 0;
}

static int async_filestream_flush(php_stream *stream)
{
	return 0;
}

static int async_filestream_cast(php_stream *stream, int castas, void **ret)
{
	return FAILURE;
}

static int async_filestream_stat(php_stream *stream, php_stream_statbuf *ssb)
{
	async_filestream_data *data;
	uv_fs_t req;

	data = (async_filestream_data *) stream->abstract;
	
	ASYNC_FS_CALL(data, &req, uv_fs_fstat, data->file);
	
	uv_fs_req_cleanup(&req);

	if (req.result < 0) {
		return 1;
	}

	map_stat(&req.statbuf, ssb);
	
	return 0;
}

static int async_filestream_seek(php_stream *stream, zend_off_t offset, int whence, zend_off_t *newoffs)
{
	async_filestream_data *data;
	php_stream_statbuf ssb;

	data = (async_filestream_data *) stream->abstract;
	
	if (0 != async_filestream_stat(stream, &ssb)) {
		return -1;
	}

	switch (whence) {
		case SEEK_SET:
			data->rpos = offset;
			break;
		case SEEK_CUR:
			data->rpos += offset;
			break;
		case SEEK_END:
			data->rpos = ssb.sb.st_size + offset;
			break;
		default:
			return -1;
	}

	*newoffs = data->rpos;
	
	if (0 == (data->mode & UV_FS_O_APPEND)) {
		data->wpos = data->rpos;
	}
	
	if (data->rpos >= 0 && data->rpos < ssb.sb.st_size) {
		data->finished = 0;
		stream->eof = 0;
	}
	
	return 0;
}

static int async_truncate(async_filestream_data *data, int64_t nsize)
{
	uv_fs_t req;

	ASYNC_FS_CALL(data, &req, uv_fs_ftruncate, data->file, nsize);

	uv_fs_req_cleanup(&req);

	return (req.result < 0) ? FAILURE : SUCCESS;
}

static int async_filestream_set_option(php_stream *stream, int option, int value, void *ptrparam)
{
	async_filestream_data *data;
	ptrdiff_t nsize;
	
	data = (async_filestream_data *) stream->abstract;
	
	switch (option) {
	case PHP_STREAM_OPTION_META_DATA_API:
		add_assoc_bool((zval *) ptrparam, "timed_out", 0);
		add_assoc_bool((zval *) ptrparam, "blocked", 1);
		add_assoc_bool((zval *) ptrparam, "eof", stream->eof);
		
		return PHP_STREAM_OPTION_RETURN_OK;
	case PHP_STREAM_OPTION_READ_BUFFER:
	 	if (value == PHP_STREAM_BUFFER_NONE) {
	 		stream->readbuf = perealloc(stream->readbuf, 0, stream->is_persistent);
	 		stream->flags |= PHP_STREAM_FLAG_NO_BUFFER;
	 	} else {
		 	stream->readbuflen = MAX(*((size_t *) ptrparam), 0x8000);
		 	stream->readbuf = perealloc(stream->readbuf, stream->readbuflen, stream->is_persistent);
		 	stream->flags &= ~PHP_STREAM_FLAG_NO_BUFFER;
	 	}
	 	
	 	return PHP_STREAM_OPTION_RETURN_OK;
	case PHP_STREAM_OPTION_LOCKING:
		if ((zend_uintptr_t) ptrparam == PHP_STREAM_LOCK_SUPPORTED) {
			return PHP_STREAM_OPTION_RETURN_OK;
		}
		
		// TODO: Check if non-blocking locks can be implemented...
		if (!flock((int) data->file, value)) {
			data->lock_flag = value;
			return PHP_STREAM_OPTION_RETURN_OK;
		}
		
		return PHP_STREAM_OPTION_RETURN_ERR;
	case PHP_STREAM_OPTION_MMAP_API:
		// TODO: Investigate support for mmap() combined with libuv.
		switch (value) {
		case PHP_STREAM_MMAP_SUPPORTED:
		case PHP_STREAM_MMAP_MAP_RANGE:
		case PHP_STREAM_MMAP_UNMAP:
			return PHP_STREAM_OPTION_RETURN_ERR;
		}
		return PHP_STREAM_OPTION_RETURN_ERR;
	case PHP_STREAM_OPTION_TRUNCATE_API:
		switch (value) {
		case PHP_STREAM_TRUNCATE_SUPPORTED:
			return PHP_STREAM_OPTION_RETURN_OK;
		case PHP_STREAM_TRUNCATE_SET_SIZE:
			nsize = *(ptrdiff_t *) ptrparam;

			if (nsize < 0) {
				return PHP_STREAM_OPTION_RETURN_ERR;
			}

			return (async_truncate(data, nsize) == SUCCESS) ? PHP_STREAM_OPTION_RETURN_OK : PHP_STREAM_OPTION_RETURN_ERR;
		}
	}

	return PHP_STREAM_OPTION_RETURN_NOTIMPL;
}

ASYNC_API const php_stream_ops async_filestream_ops = {
	async_filestream_write,
	async_filestream_read,
	async_filestream_close,
	async_filestream_flush,
	"STDIO/async",
	async_filestream_seek,
	async_filestream_cast,
	async_filestream_stat,
	async_filestream_set_option
};


static php_stream *async_filestream_wrapper_open(php_stream_wrapper *wrapper, const char *path, const char *mode,
int options, zend_string **opened_path, php_stream_context *context STREAMS_DC)
{
	async_filestream_data *data;
	zend_bool async;
	
	uv_fs_t req;

	php_stream *stream;
	char realpath[MAXPATHLEN];
	int flags;
	
	// Use original file wrapper when cast to fd is required.
	if (options & STREAM_WILL_CAST) {
		return orig_file_wrapper.wops->stream_opener(wrapper, path, mode, options, opened_path, context STREAMS_REL_CC);
	}
	
	if (FAILURE == parse_open_mode(mode, &flags)) {
		if (options & REPORT_ERRORS) {
            php_error_docref(NULL, E_WARNING, "'%s' is not a valid mode for fopen", mode);
        }
        
		return NULL;
	}

	if (((options & STREAM_DISABLE_OPEN_BASEDIR) == 0) && php_check_open_basedir(path)) {
		return NULL;
	}

	async = async_cli && ((options & STREAM_OPEN_FOR_INCLUDE) == 0);

	if (options & STREAM_ASSUME_REALPATH) {
		strlcpy(realpath, path, MAXPATHLEN);
	} else {
		if (expand_filepath(path, realpath) == NULL) {
			return NULL;
		}
	}

	ASYNC_FS_CALLW(async, &req, uv_fs_open, realpath, flags, 0666);

	uv_fs_req_cleanup(&req);
	
	if (req.result < 0) {
		if (options & REPORT_ERRORS) {
			php_error_docref(NULL, E_WARNING, "Failed to open file: %s", realpath);
		}
	
		return NULL;
	}

	data = emalloc(sizeof(async_filestream_data));
	ZEND_SECURE_ZERO(data, sizeof(async_filestream_data));

	stream = php_stream_alloc_rel(&async_filestream_ops, data, 0, mode);

	if (UNEXPECTED(stream == NULL)) {
		efree(data);
		
		return NULL;
	}
	
	// TODO: Configure default read buffer size using an INI option?
	
	stream->readbuflen = 0x8000;
 	stream->readbuf = perealloc(stream->readbuf, stream->readbuflen, stream->is_persistent);
	
	data->file = req.result;
	data->mode = flags;
	data->lock_flag = LOCK_UN;
	data->async = async;
	data->scheduler = async_task_scheduler_get();
	
	ASYNC_ADDREF(&data->scheduler->std);

	return stream;
}

static inline async_dirstream_entry *create_dir_entry(async_dirstream_entry *prev, const char *name)
{
	async_dirstream_entry *entry;

	entry = emalloc(sizeof(async_dirstream_entry) + strlen(name));
	entry->prev = prev;
	entry->next = NULL;

	strcpy(entry->name, name);

	if (prev != NULL) {
		prev->next = entry;
	}

	return entry;
}

static php_stream *async_filestream_wrapper_opendir(php_stream_wrapper *wrapper, const char *path, const char *mode,
int options, zend_string **opened_path, php_stream_context *context STREAMS_DC)
{
	async_dirstream_data *data;
	async_dirstream_entry *prev;

	uv_fs_t req;
	uv_dirent_t tmp;

	php_stream *stream;
	char realpath[MAXPATHLEN];
	int code;

	if (((options & STREAM_DISABLE_OPEN_BASEDIR) == 0) && php_check_open_basedir(path)) {
		return NULL;
	}
	
	if (options & STREAM_ASSUME_REALPATH) {
		strlcpy(realpath, path, MAXPATHLEN);
	} else {
		if (expand_filepath(path, realpath) == NULL) {
			return NULL;
		}
	}

	ASYNC_FS_CALLW(async_cli, &req, uv_fs_scandir, realpath, 0);
	
	if (req.result < 0) {
		if (options & REPORT_ERRORS) {
			php_error_docref(NULL, E_WARNING, "Failed to open dir %s: %s", realpath, uv_strerror(req.result));
		}
		
		uv_fs_req_cleanup(&req);
	
		return NULL;
	}
	
	data = emalloc(sizeof(async_dirstream_data));
	ZEND_SECURE_ZERO(data, sizeof(async_dirstream_data));
	
	stream = php_stream_alloc_rel(&async_dirstream_ops, data, 0, mode);
	
	if (UNEXPECTED(stream == NULL)) {
		efree(data);
		uv_fs_req_cleanup(&req);
		
		return NULL;
	}

	data->offset = 0;
	data->size = 2;

	prev = create_dir_entry(NULL, ".");

	data->first = prev;
	data->entry = prev;

	prev = create_dir_entry(prev, "..");

	while (1) {
		code = uv_fs_scandir_next(&req, &tmp);

		if (code == UV_EOF) {
			data->last = prev;

			break;
		}

		prev = create_dir_entry(prev, tmp.name);

		data->size++;
	}

	uv_fs_req_cleanup(&req);

	data->scheduler = async_task_scheduler_get();
	
	ASYNC_ADDREF(&data->scheduler->std);

	return stream;
}

static int async_filestream_wrapper_url_stat(php_stream_wrapper *wrapper, const char *url, int flags, php_stream_statbuf *ssb, php_stream_context *context)
{
	uv_fs_t req;
	
	char realpath[MAXPATHLEN];
	
	ASYNC_STRIP_FILE_SCHEME(url);
	
	if (php_check_open_basedir(url)) {
		return 1;
	}
	
	if (expand_filepath(url, realpath) == NULL) {
		return 1;
	}
		
	if (flags & PHP_STREAM_URL_STAT_LINK) {
		ASYNC_FS_CALLW(async_cli, &req, uv_fs_lstat, realpath);
	} else {
		ASYNC_FS_CALLW(async_cli, &req, uv_fs_stat, realpath);
	}
	
	uv_fs_req_cleanup(&req);
	
	if (req.result < 0) {
		if (flags & REPORT_ERRORS) {
			php_error_docref(NULL, E_WARNING, "Failed to stat file %s: %s", realpath, uv_strerror(req.result));
		}

		return FAILURE;;
	}

	map_stat(&req.statbuf, ssb);
	
	return SUCCESS;
}

static int async_filestream_wrapper_unlink(php_stream_wrapper *wrapper, const char *url, int options, php_stream_context *context)
{
	uv_fs_t req;
	
	char realpath[MAXPATHLEN];
	
	ASYNC_STRIP_FILE_SCHEME(url);
	
	if (((options & STREAM_DISABLE_OPEN_BASEDIR) == 0) && php_check_open_basedir(url)) {
		return 0;
	}
	
	if (expand_filepath(url, realpath) == NULL) {
		return 0;
	}
	
	ASYNC_FS_CALLW(async_cli, &req, uv_fs_unlink, realpath);
	
	uv_fs_req_cleanup(&req);
	
	if (req.result < 0) {
		if (options & REPORT_ERRORS) {
			php_error_docref(NULL, E_WARNING, "Failed to unlink file %s: %s", realpath, uv_strerror(req.result));
		}
		
		return 0;
	}
	
	php_clear_stat_cache(1, NULL, 0);
	
	return 1;
}

static int async_filestream_wrapper_rename(php_stream_wrapper *wrapper, const char *url_from, const char *url_to, int options, php_stream_context *context)
{
	uv_fs_t req;
	
	if (!url_from || !url_to) {
		return 0;
	}
	
	ASYNC_STRIP_FILE_SCHEME(url_from);
	ASYNC_STRIP_FILE_SCHEME(url_to);
	
	if (php_check_open_basedir(url_from) || php_check_open_basedir(url_to)) {
		return 0;
	}
	
	ASYNC_FS_CALLW(async_cli, &req, uv_fs_rename, url_from, url_to);
	
	uv_fs_req_cleanup(&req);
	
	if (req.result < 0) {
		if (options & REPORT_ERRORS) {
			php_error_docref(NULL, E_WARNING, "Failed to rename file %s: %s", url_from, uv_strerror(req.result));
		}
	
		return 0;
	}

	php_clear_stat_cache(1, NULL, 0);
	
	return 1;
}

static int async_mkdir(const char *url, int mode)
{
	uv_fs_t req;
	
	ASYNC_FS_CALLW(async_cli, &req, uv_fs_mkdir, url, mode);
	
	uv_fs_req_cleanup(&req);
	
	return (int) req.result;
}

static int async_filestream_wrapper_mkdir(php_stream_wrapper *wrapper, const char *dir, int mode, int options, php_stream_context *context)
{
	int ret, recursive = options & PHP_STREAM_MKDIR_RECURSIVE;
	char *p;

	ASYNC_STRIP_FILE_SCHEME(dir);

	if (!recursive) {
		ret = async_mkdir(dir, mode);
	} else {
		/* we look for directory separator from the end of string, thus hopefuly reducing our work load */
		char *e;
		zend_stat_t sb;
		size_t dir_len = strlen(dir), offset = 0;
		char buf[MAXPATHLEN];

		if (!expand_filepath_with_mode(dir, buf, NULL, 0, CWD_EXPAND )) {
			php_error_docref(NULL, E_WARNING, "Invalid path");
			return 0;
		}

		e = buf +  strlen(buf);

		if ((p = memchr(buf, DEFAULT_SLASH, dir_len))) {
			offset = p - buf + 1;
		}

		if (p && dir_len == 1) {
			/* buf == "DEFAULT_SLASH" */
		}
		else {
			/* find a top level directory we need to create */
			while ( (p = strrchr(buf + offset, DEFAULT_SLASH)) || (offset != 1 && (p = strrchr(buf, DEFAULT_SLASH))) ) {
				int n = 0;

				*p = '\0';
				while (p > buf && *(p-1) == DEFAULT_SLASH) {
					++n;
					--p;
					*p = '\0';
				}
				if (VCWD_STAT(buf, &sb) == 0) {
					while (1) {
						*p = DEFAULT_SLASH;
						if (!n) break;
						--n;
						++p;
					}
					break;
				}
			}
		}

		if (p == buf) {
			ret = async_mkdir(dir, mode);
		} else if (!(ret = async_mkdir(buf, mode))) {
			if (!p) {
				p = buf;
			}
			/* create any needed directories if the creation of the 1st directory worked */
			while (++p != e) {
				if (*p == '\0') {
					*p = DEFAULT_SLASH;
					if ((*(p+1) != '\0') && (ret = async_mkdir(buf, mode)) < 0) {
						break;
					}
				}
			}
		}
	}
	
	if (ret < 0 && options & REPORT_ERRORS) {
		php_error_docref(NULL, E_WARNING, "%s", uv_strerror(ret));
	}
	
	return (ret < 0) ? 0 : 1;
}

static int async_filestream_wrapper_rmdir(php_stream_wrapper *wrapper, const char *url, int options, php_stream_context *context)
{
	uv_fs_t req;
	
	char realpath[MAXPATHLEN];
	
	ASYNC_STRIP_FILE_SCHEME(url);
	
	if (((options & STREAM_DISABLE_OPEN_BASEDIR) == 0) && php_check_open_basedir(url)) {
		return 0;
	}
	
	if (expand_filepath(url, realpath) == NULL) {
		return 0;
	}

	ASYNC_FS_CALLW(async_cli, &req, uv_fs_rmdir, realpath);
	
	uv_fs_req_cleanup(&req);
	
	if (req.result < 0) {
		if (options & REPORT_ERRORS) {
			php_error_docref(NULL, E_WARNING, "Failed to unlink file %s: %s", realpath, uv_strerror(req.result));
		}
	
		return 0;
	}
	
	php_clear_stat_cache(1, NULL, 0);
	
	return 1;
}

static inline int async_chmod(const char *url, int mode)
{
	uv_fs_t req;
	
	ASYNC_FS_CALLW(async_cli, &req, uv_fs_access, url, mode);
	
	if (req.result < 0) {
		php_error_docref1(NULL, url, E_WARNING, "Operation failed: %s", uv_strerror(req.result));
	
		return 0;
	}

	return 1;
}

static inline int async_touch(const char *url, struct utimbuf *time)
{
	uv_fs_t req;
	
	ASYNC_FS_CALLW(async_cli, &req, uv_fs_utime, url, (double) time->actime, (double) time->modtime);
	
	uv_fs_req_cleanup(&req);
	
	if (req.result == UV_ENOENT) {
		return UV_ENOENT;
	}
	
	if (req.result < 0) {
		php_error_docref1(NULL, url, E_WARNING, "Operation failed: %s", uv_strerror(req.result));
		
		return 1;
	}
	
	return 0;
}

static inline int async_touch_create(const char *url)
{
	uv_fs_t req;
	uv_fs_t close;
	
	ASYNC_FS_CALLW(async_cli, &req, uv_fs_open, url, UV_FS_O_CREAT | UV_FS_O_APPEND, 0);
	
	uv_fs_req_cleanup(&req);
	
	if (req.result < 0) {
		php_error_docref1(NULL, url, E_WARNING, "Operation failed: %s", uv_strerror(req.result));
		
		return 0;
	}
	
	ASYNC_FS_CALLW(async_cli, &close, uv_fs_close, req.result);
	
	uv_fs_req_cleanup(&close);
	
	return 1;
}

static int async_filestream_wrapper_metadata(php_stream_wrapper *wrapper, const char *url, int option, void *value, php_stream_context *context)
{
	int ret;

	ASYNC_STRIP_FILE_SCHEME(url);

	if (php_check_open_basedir(url)) {
		return 0;
	}
	
	switch (option) {
	case PHP_STREAM_META_ACCESS:
		ret = async_chmod(url, (int) *(zend_long *) value);
	case PHP_STREAM_META_TOUCH:
		ret = async_touch(url, (struct utimbuf *) value);
		
		if (ret == UV_ENOENT) {
			ret = async_touch_create(url);
			
			if (ret == 0) {
				ret = async_touch(url, (struct utimbuf *) value);
			}
		}
	default:
		// TODO: Add non-blocking chown() and chgrp() implementations...
	
		return orig_file_wrapper.wops->stream_metadata(wrapper, url, option, value, context);
	}

	if (ret == 1) {
		php_clear_stat_cache(0, NULL, 0);
	}

	return ret;
}

ASYNC_API const php_stream_wrapper_ops async_filestream_wrapper_ops = {
	async_filestream_wrapper_open,
	NULL,
	NULL,
	async_filestream_wrapper_url_stat,
	async_filestream_wrapper_opendir,
	"plainfile/async",
	async_filestream_wrapper_unlink,
	async_filestream_wrapper_rename,
	async_filestream_wrapper_mkdir,
	async_filestream_wrapper_rmdir,
	async_filestream_wrapper_metadata
};

static php_stream_wrapper async_filestream_wrapper = {
	&async_filestream_wrapper_ops,
	NULL,
	0
};


void async_filesystem_init()
{
	// This works starting with PHP 7.3.0RC5.
	// It relies on commit 770fe51bfd8994c3df819cbf04b7d76824b55e5c by dstogov (2018-10-24).

	if (async_cli) {
		orig_file_wrapper = php_plain_files_wrapper;
		php_plain_files_wrapper = async_filestream_wrapper;
	}
}

void async_filesystem_shutdown()
{
	if (async_cli) {
		php_plain_files_wrapper = orig_file_wrapper;
	}
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
