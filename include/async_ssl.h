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

#ifndef ASYNC_SSL_H
#define ASYNC_SSL_H

#define ASYNC_SSL_MODE_SERVER 0
#define ASYNC_SSL_MODE_CLIENT 1

#define ASYNC_SSL_DEFAULT_VERIFY_DEPTH 9

#ifndef OPENSSL_NO_TLSEXT
#define ASYNC_TLS_SNI 1
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
#define ASYNC_TLS_ALPN 1
#endif
#endif

typedef struct {
	/* SSL mode (ASYNC_SSL_MODE_SERVER or ASYNC_SSL_MODE_CLIENT). */
	zend_bool mode;

	/* If set self-signed server certificates are accepted. */
	zend_bool allow_self_signed;

	/* Name of the peer to connect to. */
	zend_string *peer_name;

	/* Maximum verification cert chain length */
	int verify_depth;
} async_ssl_settings;

typedef struct {
	async_op base;
	int uv_error;
	int ssl_error;
} async_ssl_op;

typedef struct {
	async_ssl_settings *settings;
	zend_string *host;
	int uv_error;
	int ssl_error;
	zend_string *error;
} async_ssl_handshake_data;

typedef struct {
#ifdef HAVE_ASYNC_SSL
	/* SSL context.*/
	SSL_CTX *ctx;

	/* SSL encryption engine. */
	SSL *ssl;

	/* Holds bytes that need to be processed as SSL input data. */
	BIO *rbio;

	/* Holds processed encrypted bytes that need to be sent. */
	BIO *wbio;
	
	/* Number of available (decoded) input bytes. */
	size_t available;
	
	/* Number of consumed input bytes of an unfinished packet. */
	size_t pending;
	
	/* Current handshake operation. */
	async_ssl_op *handshake;

	/* SSL connection and encryption settings. */
	async_ssl_settings settings;
#endif
} async_ssl_engine;

typedef struct _async_tls_cert async_tls_cert;

struct _async_tls_cert {
	zend_string *host;
	zend_string *file;
	zend_string *key;
	zend_string *passphrase;
	async_tls_cert *next;
	async_tls_cert *prev;
#ifdef HAVE_ASYNC_SSL
	SSL_CTX *ctx;
#endif
};

typedef struct {
	async_tls_cert *first;
	async_tls_cert *last;
} async_tls_cert_queue;

typedef struct {
	/* PHP object handle. */
	zend_object std;

	async_ssl_settings settings;
} async_tls_client_encryption;

typedef struct {
	/* PHP object handle. */
	zend_object std;

	async_tls_cert cert;
	async_tls_cert_queue certs;
} async_tls_server_encryption;

#ifdef HAVE_ASYNC_SSL
SSL_CTX *async_ssl_create_context();
int async_ssl_create_engine(async_ssl_engine *engine);
void async_ssl_dispose_engine(async_ssl_engine *engine, zend_bool ctx);

void async_ssl_setup_sni(SSL_CTX *ctx, async_tls_server_encryption *encryption);
void async_ssl_setup_verify_callback(SSL_CTX *ctx, async_ssl_settings *settings);
int async_ssl_setup_encryption(SSL *ssl, async_ssl_settings *settings);
#endif

async_tls_client_encryption *async_clone_client_encryption(async_tls_client_encryption *encryption);

#endif
