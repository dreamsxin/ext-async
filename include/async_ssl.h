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

SSL_CTX *async_ssl_create_context();

int async_ssl_error_continue(SSL *ssl, int code);

void async_ssl_setup_sni(SSL_CTX *ctx, async_tls_server_encryption *encryption);
void async_ssl_setup_verify_callback(SSL_CTX *ctx, async_tls_client_encryption *encryption);
int async_ssl_setup_encryption(SSL *ssl, async_tls_client_encryption *encryption);

async_tls_client_encryption *async_clone_client_encryption(async_tls_client_encryption *encryption);

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
