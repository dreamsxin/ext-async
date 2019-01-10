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
#include "async_ssl.h"

ASYNC_API zend_class_entry *async_tls_client_encryption_ce;
ASYNC_API zend_class_entry *async_tls_server_encryption_ce;

static zend_object_handlers async_tls_client_encryption_handlers;
static zend_object_handlers async_tls_server_encryption_handlers;

#ifdef HAVE_ASYNC_SSL

#ifdef PHP_WIN32
#include "win32/winutil.h"
#include "win32/time.h"
#include <Wincrypt.h>
/* These are from Wincrypt.h, they conflict with OpenSSL */
#undef X509_NAME
#undef X509_CERT_PAIR
#undef X509_EXTENSIONS
#endif

#define ASYNC_SSL_RETURN_VERIFY_ERROR(ctx) do { \
	X509_STORE_CTX_set_error(ctx, X509_V_ERR_APPLICATION_VERIFICATION); \
	return 0; \
} while (0);

#define ASYNC_PROPAGATE_ERROR(engine, code) do { \
	const char *tmp; \
	(engine)->error = SSL_get_error((engine)->ssl, code); \
	if ((engine)->message != NULL) { \
		efree((engine)->message); \
	} \
	tmp = ERR_reason_error_string((engine)->error); \
	if (tmp && tmp != NULL) { \
		(engine)->message = emalloc(4096); \
		strcpy((engine)->message, tmp); \
	} \
} while (0)

#define ASYNC_PROPAGATE_UV_ERROR(engine, code) do { \
	(engine)->error = code; \
	if ((engine)->message != NULL) { \
		efree((engine)->message); \
		(engine)->message = NULL; \
	} \
	(engine)->message = emalloc(4096); \
	strcpy((engine)->message, uv_strerror(code)); \
} while (0)

static int async_index;

static int ssl_cert_passphrase_cb(char *buf, int size, int rwflag, void *obj)
{
	async_tls_cert *cert;

	cert = (async_tls_cert *) obj;

	if (cert == NULL || cert->passphrase == NULL) {
		return 0;
	}

	strcpy(buf, ZSTR_VAL(cert->passphrase));

	return ZSTR_LEN(cert->passphrase);
}

static int ssl_servername_cb(SSL *ssl, int *ad, void *arg)
{
	async_tls_cert_queue *q;
	async_tls_cert *cert;
	zend_string *key;

	const char *name;

	if (ssl == NULL) {
		return SSL_TLSEXT_ERR_NOACK;
	}

	name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);

	if (name == NULL || name[0] == '\0') {
		return SSL_TLSEXT_ERR_NOACK;
	}

	q = (async_tls_cert_queue *) arg;
	key = zend_string_init(name, strlen(name), 0);

	cert = q->first;

	while (cert != NULL) {
		if (zend_string_equals(key, cert->host)) {
			SSL_set_SSL_CTX(ssl, cert->ctx);
			break;
		}

		cert = cert->next;
	}

	zend_string_release(key);

	return SSL_TLSEXT_ERR_OK;
}

static zend_bool async_ssl_match_hostname(const char *subjectname, const char *certname)
{
	char *wildcard = NULL;
	ptrdiff_t prefix_len;
	size_t suffix_len, subject_len;

	if (strcasecmp(subjectname, certname) == 0) {
		return 1;
	}

	if (!(wildcard = strchr(certname, '*')) || memchr(certname, '.', wildcard - certname)) {
		return 0;
	}

	prefix_len = wildcard - certname;
	if (prefix_len && strncasecmp(subjectname, certname, prefix_len) != 0) {
		return 0;
	}

	suffix_len = strlen(wildcard + 1);
	subject_len = strlen(subjectname);

	if (suffix_len <= subject_len) {
		return strcasecmp(wildcard + 1, subjectname + subject_len - suffix_len) == 0 && memchr(subjectname + prefix_len, '.', subject_len - suffix_len - prefix_len) == NULL;
	}

	return strcasecmp(wildcard + 2, subjectname + subject_len - suffix_len);
}

static int async_ssl_check_san_names(zend_string *peer_name, X509 *cert, X509_STORE_CTX *ctx)
{
	GENERAL_NAMES *names;
	GENERAL_NAME *entry;

	unsigned char *cn;
	int count;
	int i;

	names = X509_get_ext_d2i(cert, NID_subject_alt_name, 0, 0);

	if (names == NULL) {
		return 0;
	}

	for (count = sk_GENERAL_NAME_num(names), i = 0; i < count; i++) {
		entry = sk_GENERAL_NAME_value(names, i);

		if (entry == NULL || GEN_DNS != entry->type) {
			continue;
		}

		ASN1_STRING_to_UTF8(&cn, entry->d.dNSName);

		if ((size_t) ASN1_STRING_length(entry->d.dNSName) != strlen((const char *) cn)) {
			OPENSSL_free(cn);
			break;
		}
		
		if (async_ssl_match_hostname(ZSTR_VAL(peer_name), (const char *) cn)) {
			OPENSSL_free(cn);
			sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);
			return 1;
		}

		OPENSSL_free(cn);
	}

	sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);

	return 0;
}

static int ssl_verify_callback(int preverify, X509_STORE_CTX *ctx)
{
	async_ssl_settings *settings;
	SSL *ssl;

	X509 *cert;
	X509_NAME_ENTRY *entry;
	ASN1_STRING *str;

	unsigned char *cn;

	int depth;
	int err;
	int i;

	cert = X509_STORE_CTX_get_current_cert(ctx);
	depth = X509_STORE_CTX_get_error_depth(ctx);
	err = X509_STORE_CTX_get_error(ctx);

	ssl = X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
	settings = (async_ssl_settings *) SSL_get_ex_data(ssl, async_index);

	if (depth == 0 && err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT) {
		if (settings != NULL && settings->allow_self_signed) {
			err = 0;
			preverify = 1;
			X509_STORE_CTX_set_error(ctx, X509_V_OK);
		}
	}

	if (depth > settings->verify_depth) {
		X509_STORE_CTX_set_error(ctx, X509_V_ERR_CERT_CHAIN_TOO_LONG);
		return 0;
	}

	if (!cert || err || settings == NULL) {
		return preverify;
	}

	if (depth == 0) {
		if (async_ssl_check_san_names(settings->peer_name, cert, ctx)) {
			return preverify;
		}

		if ((i = X509_NAME_get_index_by_NID(X509_get_subject_name((X509 *) cert), NID_commonName, -1)) < 0) {
			ASYNC_SSL_RETURN_VERIFY_ERROR(ctx);
		}

		if (NULL == (entry = X509_NAME_get_entry(X509_get_subject_name((X509 *) cert), i))) {
			ASYNC_SSL_RETURN_VERIFY_ERROR(ctx);
		}

		if (NULL == (str = X509_NAME_ENTRY_get_data(entry))) {
			ASYNC_SSL_RETURN_VERIFY_ERROR(ctx);
		}

		ASN1_STRING_to_UTF8(&cn, str);

		if ((size_t) ASN1_STRING_length(str) != strlen((const char *) cn)) {
			OPENSSL_free(cn);
			ASYNC_SSL_RETURN_VERIFY_ERROR(ctx);
		}

		if (!async_ssl_match_hostname(ZSTR_VAL(settings->peer_name), (const char *) cn)) {
			OPENSSL_free(cn);
			ASYNC_SSL_RETURN_VERIFY_ERROR(ctx);
		}

		OPENSSL_free(cn);
	}

	return preverify;
}

SSL_CTX *async_ssl_create_context()
{
	SSL_CTX *ctx;

	int options;
	char *cadir;

	options = SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3;
	options |= SSL_OP_NO_COMPRESSION | SSL_OP_NO_TICKET;
	options &= ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS;

	ctx = SSL_CTX_new(SSLv23_method());

	SSL_CTX_set_options(ctx, options);
	SSL_CTX_set_cipher_list(ctx, "HIGH:!SSLv2:!aNULL:!eNULL:!EXPORT:!DES:!MD5:!RC4:!ADH");

	// Use default X509 cert directory.
	cadir = getenv(X509_get_default_cert_dir_env());
	SSL_CTX_load_verify_locations(ctx, NULL, (cadir == NULL) ? X509_get_default_cert_dir() : cadir);

	SSL_CTX_set_default_passwd_cb(ctx, ssl_cert_passphrase_cb);

	return ctx;
}

static int configure_engine(SSL_CTX *ctx, SSL *ssl, BIO *rbio, BIO *wbio)
{
	BIO_set_mem_eof_return(rbio, -1);
	BIO_set_mem_eof_return(wbio, -1);

	SSL_set_bio(ssl, rbio, wbio);
	SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE);

#ifdef SSL_MODE_RELEASE_BUFFERS
	SSL_set_mode(ssl, SSL_get_mode(ssl) | SSL_MODE_RELEASE_BUFFERS);
#endif

	SSL_set_read_ahead(ssl, 1);

	return SUCCESS;
}

int async_ssl_create_engine(async_ssl_engine *engine)
{
	engine->ssl = SSL_new(engine->ctx);
	engine->rbio = BIO_new(BIO_s_mem());
	engine->wbio = BIO_new(BIO_s_mem());
	
	return configure_engine(engine->ctx, engine->ssl, engine->rbio, engine->wbio);
}

void async_ssl_dispose_engine(async_ssl_engine *engine, zend_bool ctx)
{
	if (engine->ssl != NULL) {
		SSL_free(engine->ssl);
	}

	if (engine->ctx != NULL && ctx) {
		SSL_CTX_free(engine->ctx);
	}
	
	if (engine->settings.peer_name != NULL) {
		zend_string_release(engine->settings.peer_name);
		engine->settings.peer_name = NULL;
	}
}

#ifdef PHP_WIN32

#define RETURN_CERT_VERIFY_FAILURE(code) do { \
	X509_STORE_CTX_set_error(x509_store_ctx, code); \
	return 0; \
} while (0)

#define PHP_X509_NAME_ENTRY_TO_UTF8(ne, i, out) ASN1_STRING_to_UTF8(&out, X509_NAME_ENTRY_get_data(X509_NAME_get_entry(ne, i)))

#define php_win_err_free(err) do { \
	if (err && err[0]) \
		free(err); \
} while (0)

static int ssl_win32_verify_callback(X509_STORE_CTX *x509_store_ctx, void *arg)
{
	PCCERT_CONTEXT cert_ctx = NULL;
	PCCERT_CHAIN_CONTEXT cert_chain_ctx = NULL;
#if OPENSSL_VERSION_NUMBER < 0x10100000L
	X509 *cert = x509_store_ctx->cert;
#else
	X509 *cert = X509_STORE_CTX_get0_cert(x509_store_ctx);
#endif

	async_ssl_settings *settings;
	SSL* ssl;
	zend_bool is_self_signed = 0;

	ssl = X509_STORE_CTX_get_ex_data(x509_store_ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
	settings = (async_ssl_settings *) SSL_get_ex_data(ssl, async_index);

	{ /* First convert the x509 struct back to a DER encoded buffer and let Windows decode it into a form it can work with */
		unsigned char *der_buf = NULL;
		int der_len;

		der_len = i2d_X509(cert, &der_buf);
		if (der_len < 0) {
			unsigned long err_code, e;
			char err_buf[512];

			while ((e = ERR_get_error()) != 0) {
				err_code = e;
			}

			php_error_docref(NULL, E_WARNING, "Error encoding X509 certificate: %d: %s", err_code, ERR_error_string(err_code, err_buf));
			RETURN_CERT_VERIFY_FAILURE(SSL_R_CERTIFICATE_VERIFY_FAILED);
		}

		cert_ctx = CertCreateCertificateContext(X509_ASN_ENCODING, der_buf, der_len);
		OPENSSL_free(der_buf);

		if (cert_ctx == NULL) {
			char *err = php_win_err();
			php_error_docref(NULL, E_WARNING, "Error creating certificate context: %s", err);
			php_win_err_free(err);
			RETURN_CERT_VERIFY_FAILURE(SSL_R_CERTIFICATE_VERIFY_FAILED);
		}
	}

	{ /* Next fetch the relevant cert chain from the store */
		CERT_ENHKEY_USAGE enhkey_usage = {0};
		CERT_USAGE_MATCH cert_usage = {0};
		CERT_CHAIN_PARA chain_params = {sizeof(CERT_CHAIN_PARA)};
		LPSTR usages[] = {szOID_PKIX_KP_SERVER_AUTH, szOID_SERVER_GATED_CRYPTO, szOID_SGC_NETSCAPE};
		DWORD chain_flags = 0;
		unsigned long allowed_depth;
		unsigned int i;

		enhkey_usage.cUsageIdentifier = 3;
		enhkey_usage.rgpszUsageIdentifier = usages;
		cert_usage.dwType = USAGE_MATCH_TYPE_OR;
		cert_usage.Usage = enhkey_usage;
		chain_params.RequestedUsage = cert_usage;
		chain_flags = CERT_CHAIN_CACHE_END_CERT | CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;

		if (!CertGetCertificateChain(NULL, cert_ctx, NULL, NULL, &chain_params, chain_flags, NULL, &cert_chain_ctx)) {
			char *err = php_win_err();
			php_error_docref(NULL, E_WARNING, "Error getting certificate chain: %s", err);
			php_win_err_free(err);
			CertFreeCertificateContext(cert_ctx);
			RETURN_CERT_VERIFY_FAILURE(SSL_R_CERTIFICATE_VERIFY_FAILED);
		}

		/* check if the cert is self-signed */
		if (cert_chain_ctx->cChain > 0 && cert_chain_ctx->rgpChain[0]->cElement > 0
			&& (cert_chain_ctx->rgpChain[0]->rgpElement[0]->TrustStatus.dwInfoStatus & CERT_TRUST_IS_SELF_SIGNED) != 0) {
			is_self_signed = 1;
		}

		/* check the depth */
		allowed_depth = settings->verify_depth;

		for (i = 0; i < cert_chain_ctx->cChain; i++) {
			if (cert_chain_ctx->rgpChain[i]->cElement > allowed_depth) {
				CertFreeCertificateChain(cert_chain_ctx);
				CertFreeCertificateContext(cert_ctx);
				RETURN_CERT_VERIFY_FAILURE(X509_V_ERR_CERT_CHAIN_TOO_LONG);
			}
		}
	}

	{ /* Then verify it against a policy */
		SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl_policy_params = {sizeof(SSL_EXTRA_CERT_CHAIN_POLICY_PARA)};
		CERT_CHAIN_POLICY_PARA chain_policy_params = {sizeof(CERT_CHAIN_POLICY_PARA)};
		CERT_CHAIN_POLICY_STATUS chain_policy_status = {sizeof(CERT_CHAIN_POLICY_STATUS)};
		LPWSTR server_name = NULL;
		BOOL verify_result;

		{ /* This looks ridiculous and it is - but we validate the name ourselves using the peer_name
		     ctx option, so just use the CN from the cert here */

			X509_NAME *cert_name;
			unsigned char *cert_name_utf8;
			int index, cert_name_utf8_len;
			DWORD num_wchars;

			cert_name = X509_get_subject_name(cert);
			index = X509_NAME_get_index_by_NID(cert_name, NID_commonName, -1);
			if (index < 0) {
				php_error_docref(NULL, E_WARNING, "Unable to locate certificate CN");
				CertFreeCertificateChain(cert_chain_ctx);
				CertFreeCertificateContext(cert_ctx);
				RETURN_CERT_VERIFY_FAILURE(SSL_R_CERTIFICATE_VERIFY_FAILED);
			}

			cert_name_utf8_len = PHP_X509_NAME_ENTRY_TO_UTF8(cert_name, index, cert_name_utf8);

			num_wchars = MultiByteToWideChar(CP_UTF8, 0, (char*)cert_name_utf8, -1, NULL, 0);
			if (num_wchars == 0) {
				php_error_docref(NULL, E_WARNING, "Unable to convert %s to wide character string", cert_name_utf8);
				OPENSSL_free(cert_name_utf8);
				CertFreeCertificateChain(cert_chain_ctx);
				CertFreeCertificateContext(cert_ctx);
				RETURN_CERT_VERIFY_FAILURE(SSL_R_CERTIFICATE_VERIFY_FAILED);
			}

			server_name = emalloc((num_wchars * sizeof(WCHAR)) + sizeof(WCHAR));

			num_wchars = MultiByteToWideChar(CP_UTF8, 0, (char*)cert_name_utf8, -1, server_name, num_wchars);
			if (num_wchars == 0) {
				php_error_docref(NULL, E_WARNING, "Unable to convert %s to wide character string", cert_name_utf8);
				efree(server_name);
				OPENSSL_free(cert_name_utf8);
				CertFreeCertificateChain(cert_chain_ctx);
				CertFreeCertificateContext(cert_ctx);
				RETURN_CERT_VERIFY_FAILURE(SSL_R_CERTIFICATE_VERIFY_FAILED);
			}

			OPENSSL_free(cert_name_utf8);
		}

		ssl_policy_params.dwAuthType = (settings->mode == ASYNC_SSL_MODE_CLIENT) ? AUTHTYPE_CLIENT : AUTHTYPE_SERVER;
		ssl_policy_params.pwszServerName = server_name;
		chain_policy_params.pvExtraPolicyPara = &ssl_policy_params;

		verify_result = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, cert_chain_ctx, &chain_policy_params, &chain_policy_status);

		efree(server_name);
		CertFreeCertificateChain(cert_chain_ctx);
		CertFreeCertificateContext(cert_ctx);

		if (!verify_result) {
			char *err = php_win_err();
			php_error_docref(NULL, E_WARNING, "Error verifying certificate chain policy: %s", err);
			php_win_err_free(err);
			RETURN_CERT_VERIFY_FAILURE(SSL_R_CERTIFICATE_VERIFY_FAILED);
		}

		if (chain_policy_status.dwError != 0) {
			/* The chain does not match the policy */
			if (is_self_signed && chain_policy_status.dwError == CERT_E_UNTRUSTEDROOT && settings->allow_self_signed) {
				/* allow self-signed certs */
				X509_STORE_CTX_set_error(x509_store_ctx, X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT);
			} else {
				RETURN_CERT_VERIFY_FAILURE(SSL_R_CERTIFICATE_VERIFY_FAILED);
			}
		}
	}

	return 1;
}

#endif

void async_ssl_setup_verify_callback(SSL_CTX *ctx, async_ssl_settings *settings)
{
	SSL_CTX_set_default_passwd_cb_userdata(ctx, NULL);
	SSL_CTX_set_verify_depth(ctx, settings->verify_depth);

#ifdef PHP_WIN32
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
		SSL_CTX_set_cert_verify_callback(ctx, ssl_win32_verify_callback, settings);
#else
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, ssl_verify_callback);
#endif
}

int async_ssl_setup_encryption(SSL *ssl, async_ssl_settings *settings)
{
	return SSL_set_ex_data(ssl, async_index, settings);
}

void async_ssl_setup_sni(SSL_CTX *ctx, async_tls_server_encryption *encryption)
{
	SSL_CTX_set_tlsext_servername_callback(ctx, ssl_servername_cb);
	SSL_CTX_set_tlsext_servername_arg(ctx, &encryption->certs);
}

#endif


static zend_object *async_tls_client_encryption_object_create(zend_class_entry *ce)
{
	async_tls_client_encryption *encryption;

	encryption = emalloc(sizeof(async_tls_client_encryption));
	ZEND_SECURE_ZERO(encryption, sizeof(async_tls_client_encryption));

	zend_object_std_init(&encryption->std, ce);
	encryption->std.handlers = &async_tls_client_encryption_handlers;

	encryption->settings.verify_depth = ASYNC_SSL_DEFAULT_VERIFY_DEPTH;

	return &encryption->std;
}

async_tls_client_encryption *async_clone_client_encryption(async_tls_client_encryption *encryption)
{
	async_tls_client_encryption *result;

	result = (async_tls_client_encryption *) async_tls_client_encryption_object_create(async_tls_client_encryption_ce);
	result->settings.allow_self_signed = encryption->settings.allow_self_signed;
	result->settings.verify_depth = encryption->settings.verify_depth;

	if (encryption->settings.peer_name != NULL) {
		result->settings.peer_name = zend_string_copy(encryption->settings.peer_name);
	}

	return result;
}

static void async_tls_client_encryption_object_destroy(zend_object *object)
{
	async_tls_client_encryption *encryption;

	encryption = (async_tls_client_encryption *) object;

	if (encryption->settings.peer_name != NULL) {
		zend_string_release(encryption->settings.peer_name);
	}

	zend_object_std_dtor(&encryption->std);
}

ZEND_METHOD(TlsClientEncryption, withAllowSelfSigned)
{
	async_tls_client_encryption *encryption;

	zend_bool allow;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_BOOL(allow)
	ZEND_PARSE_PARAMETERS_END();

	encryption = async_clone_client_encryption((async_tls_client_encryption *) Z_OBJ_P(getThis()));
	encryption->settings.allow_self_signed = allow;

	ZVAL_OBJ(&obj, &encryption->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(TlsClientEncryption, withVerifyDepth)
{
	async_tls_client_encryption *encryption;

	zend_long depth;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_LONG(depth)
	ZEND_PARSE_PARAMETERS_END();

	ASYNC_CHECK_ERROR(depth < 1, "Verify depth must not be less than 1");

	encryption = async_clone_client_encryption((async_tls_client_encryption *) Z_OBJ_P(getThis()));
	encryption->settings.verify_depth = (int) depth;

	ZVAL_OBJ(&obj, &encryption->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(TlsClientEncryption, withPeerName)
{
	async_tls_client_encryption *encryption;

	zend_string *name;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_STR(name)
	ZEND_PARSE_PARAMETERS_END();

	encryption = async_clone_client_encryption((async_tls_client_encryption *) Z_OBJ_P(getThis()));
	encryption->settings.peer_name = zend_string_copy(name);

	ZVAL_OBJ(&obj, &encryption->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tls_client_encryption_with_allow_self_signed, 0, 1, Concurrent\\Network\\TlsClientEncryption, 0)
	ZEND_ARG_TYPE_INFO(0, allow, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tls_client_encryption_with_verify_depth, 0, 1, Concurrent\\Network\\TlsClientEncryption, 0)
	ZEND_ARG_TYPE_INFO(0, depth, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tls_client_encryption_with_peer_name, 0, 1, Concurrent\\Network\\TlsClientEncryption, 0)
	ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_tls_client_encryption_functions[] = {
	ZEND_ME(TlsClientEncryption, withAllowSelfSigned, arginfo_tls_client_encryption_with_allow_self_signed, ZEND_ACC_PUBLIC)
	ZEND_ME(TlsClientEncryption, withVerifyDepth, arginfo_tls_client_encryption_with_verify_depth, ZEND_ACC_PUBLIC)
	ZEND_ME(TlsClientEncryption, withPeerName, arginfo_tls_client_encryption_with_peer_name, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static zend_object *async_tls_server_encryption_object_create(zend_class_entry *ce)
{
	async_tls_server_encryption *encryption;

	encryption = emalloc(sizeof(async_tls_server_encryption));
	ZEND_SECURE_ZERO(encryption, sizeof(async_tls_server_encryption));

	zend_object_std_init(&encryption->std, ce);
	encryption->std.handlers = &async_tls_server_encryption_handlers;

	return &encryption->std;
}

static async_tls_server_encryption *clone_server_encryption(async_tls_server_encryption *encryption)
{
	async_tls_server_encryption *result;

	result = (async_tls_server_encryption *) async_tls_server_encryption_object_create(async_tls_server_encryption_ce);

	if (encryption->cert.file != NULL) {
		result->cert.file = zend_string_copy(encryption->cert.file);
	}

	if (encryption->cert.key != NULL) {
		result->cert.key = zend_string_copy(encryption->cert.key);
	}

	if (encryption->cert.passphrase != NULL) {
		result->cert.passphrase = zend_string_copy(encryption->cert.passphrase);
	}

	return result;
}

static void async_tls_server_encryption_object_destroy(zend_object *object)
{
	async_tls_server_encryption *encryption;

	encryption = (async_tls_server_encryption *) object;

	if (encryption->cert.file != NULL) {
		zend_string_release(encryption->cert.file);
	}

	if (encryption->cert.key != NULL) {
		zend_string_release(encryption->cert.key);
	}

	if (encryption->cert.passphrase != NULL) {
		zend_string_release(encryption->cert.passphrase);
	}

#ifdef HAVE_ASYNC_SSL
	if (encryption->cert.ctx != NULL) {
		SSL_CTX_free(encryption->cert.ctx);
	}
#endif

	zend_object_std_dtor(&encryption->std);
}

ZEND_METHOD(TlsServerEncryption, withDefaultCertificate)
{
	async_tls_server_encryption *encryption;

	zend_string *file;
	zend_string *key;
	zend_string *passphrase;

	zval obj;

	passphrase = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 3)
		Z_PARAM_STR(file)
		Z_PARAM_STR(key)
		Z_PARAM_OPTIONAL
		Z_PARAM_STR(passphrase)
	ZEND_PARSE_PARAMETERS_END();

	encryption = clone_server_encryption((async_tls_server_encryption *) Z_OBJ_P(getThis()));

	encryption->cert.file = zend_string_copy(file);
	encryption->cert.key = zend_string_copy(key);

	if (passphrase != NULL) {
		encryption->cert.passphrase = zend_string_copy(passphrase);
	}

	ZVAL_OBJ(&obj, &encryption->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(TlsServerEncryption, withCertificate)
{
	async_tls_server_encryption *encryption;
	async_tls_cert *cert;
	async_tls_cert *current;

	zend_string *host;
	zend_string *file;
	zend_string *key;
	zend_string *passphrase;

	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 3, 4)
		Z_PARAM_STR(host)
		Z_PARAM_STR(file)
		Z_PARAM_STR(key)
		Z_PARAM_OPTIONAL
		Z_PARAM_STR(passphrase)
	ZEND_PARSE_PARAMETERS_END();

	encryption = clone_server_encryption((async_tls_server_encryption *) Z_OBJ_P(getThis()));

	cert = emalloc(sizeof(async_tls_cert));
	ZEND_SECURE_ZERO(cert, sizeof(async_tls_cert));

	cert->host = zend_string_copy(host);
	cert->file = zend_string_copy(file);
	cert->key = zend_string_copy(key);

	if (passphrase != NULL) {
		cert->passphrase = zend_string_copy(passphrase);
	}

#ifdef HAVE_ASYNC_SSL
	cert->ctx = async_ssl_create_context();

	SSL_CTX_set_default_passwd_cb(cert->ctx, ssl_cert_passphrase_cb);
	SSL_CTX_set_default_passwd_cb_userdata(cert->ctx, cert);

	SSL_CTX_use_certificate_file(cert->ctx, ZSTR_VAL(cert->file), SSL_FILETYPE_PEM);
	SSL_CTX_use_PrivateKey_file(cert->ctx, ZSTR_VAL(cert->key), SSL_FILETYPE_PEM);
#endif

	current = encryption->certs.first;

	while (current != NULL) {
		if (zend_string_equals(current->host, cert->host)) {
			ASYNC_Q_DETACH(&encryption->certs, current);
			break;
		}

		current = current->next;
	}

	ASYNC_Q_ENQUEUE(&encryption->certs, cert);

	ZVAL_OBJ(&obj, &encryption->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tls_server_encryption_with_default_certificate, 0, 2, Concurrent\\Network\\TlsServerEncryption, 0)
	ZEND_ARG_TYPE_INFO(0, cert, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, passphrase, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_tls_server_encryption_with_certificate, 0, 3, Concurrent\\Network\\TlsServerEncryption, 0)
	ZEND_ARG_TYPE_INFO(0, host, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, cert, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, passphrase, IS_STRING, 1)
ZEND_END_ARG_INFO()

static const zend_function_entry async_tls_server_encryption_functions[] = {
	ZEND_ME(TlsServerEncryption, withDefaultCertificate, arginfo_tls_server_encryption_with_default_certificate, ZEND_ACC_PUBLIC)
	ZEND_ME(TlsServerEncryption, withCertificate, arginfo_tls_server_encryption_with_certificate, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void async_ssl_ce_register()
{
	zend_class_entry ce;
	
	INIT_CLASS_ENTRY(ce, "Concurrent\\Network\\TlsClientEncryption", async_tls_client_encryption_functions);
	async_tls_client_encryption_ce = zend_register_internal_class(&ce);
	async_tls_client_encryption_ce->ce_flags |= ZEND_ACC_FINAL;
	async_tls_client_encryption_ce->create_object = async_tls_client_encryption_object_create;

	memcpy(&async_tls_client_encryption_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_tls_client_encryption_handlers.free_obj = async_tls_client_encryption_object_destroy;
	async_tls_client_encryption_handlers.clone_obj = NULL;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Network\\TlsServerEncryption", async_tls_server_encryption_functions);
	async_tls_server_encryption_ce = zend_register_internal_class(&ce);
	async_tls_server_encryption_ce->ce_flags |= ZEND_ACC_FINAL;
	async_tls_server_encryption_ce->create_object = async_tls_server_encryption_object_create;

	memcpy(&async_tls_server_encryption_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_tls_server_encryption_handlers.free_obj = async_tls_server_encryption_object_destroy;
	async_tls_server_encryption_handlers.clone_obj = NULL;
	
#ifdef HAVE_ASYNC_SSL
	async_index = SSL_get_ex_new_index(0, "async", NULL, NULL, NULL);
#endif
}

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
