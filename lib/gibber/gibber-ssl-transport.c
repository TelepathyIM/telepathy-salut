/*
 * gibber-ssl-transport.c - Source for GibberSSLTransport based on
 * lm-ssl-openssl from loudmouth
 * Copyright (C) 2006 Imendio AB
 * Copyright (C) 2006 Nokia Corporation. All rights reserved.
 * Copyright (C) 2006 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd@luon.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include <stdio.h>
#include <stdlib.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "gibber-fd-transport.h"
#include "gibber-ssl-transport.h"

#define DEBUG_FLAG DEBUG_SSL
#include "gibber-debug.h"

G_DEFINE_TYPE(GibberSSLTransport, gibber_ssl_transport, GIBBER_TYPE_TRANSPORT)

/* signal enum */
#if 0
enum
{
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
#endif

/* private structure */
typedef struct _GibberSSLTransportPrivate GibberSSLTransportPrivate;

GQuark
gibber_ssl_transport_error_quark (void) {
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("salut_ssl_transport_error");

  return quark;
}


struct _GibberSSLTransportPrivate {
  gboolean dispose_has_run;
  /* The underlying transport */
  GibberTransport *transport;
  SSL_METHOD *ssl_method;
  SSL_CTX *ssl_ctx;
  SSL *ssl;
  BIO *rbio;
  BIO *wbio;
#ifdef HAVE_CST
  Cst *cst;
#endif
};

#define GIBBER_SSL_TRANSPORT_GET_PRIVATE(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), GIBBER_TYPE_SSL_TRANSPORT, GibberSSLTransportPrivate))

static void
gibber_ssl_transport_init (GibberSSLTransport *obj)
{
  //GibberSSLTransportPrivate *priv = GIBBER_SSL_TRANSPORT_GET_PRIVATE (obj);

  /* allocate any data required by the object here */
}

static void gibber_ssl_transport_dispose (GObject *object);
static void gibber_ssl_transport_finalize (GObject *object);

static gboolean
ssl_transport_send(GibberTransport *transport, 
                        const guint8 *data, gsize size,
                        GError **error);
static void
ssl_transport_disconnect(GibberTransport *transport);

static void
gibber_ssl_transport_class_init (GibberSSLTransportClass *gibber_ssl_transport_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_ssl_transport_class);
  GibberTransportClass *transport_class =
      GIBBER_TRANSPORT_CLASS(gibber_ssl_transport_class);

  g_type_class_add_private (gibber_ssl_transport_class, sizeof (GibberSSLTransportPrivate));

  object_class->dispose = gibber_ssl_transport_dispose;
  object_class->finalize = gibber_ssl_transport_finalize;

  transport_class->send = ssl_transport_send;
  transport_class->disconnect = ssl_transport_disconnect;
}

void
gibber_ssl_transport_dispose (GObject *object)
{
  GibberSSLTransport *self = GIBBER_SSL_TRANSPORT (object);
  GibberSSLTransportPrivate *priv = GIBBER_SSL_TRANSPORT_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->transport != NULL) {
    g_object_unref(priv->transport);
    priv->transport = NULL;
  }

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (gibber_ssl_transport_parent_class)->dispose)
    G_OBJECT_CLASS (gibber_ssl_transport_parent_class)->dispose (object);
}

void
gibber_ssl_transport_finalize (GObject *object)
{
  GibberSSLTransport *self = GIBBER_SSL_TRANSPORT (object);
  GibberSSLTransportPrivate *priv = GIBBER_SSL_TRANSPORT_GET_PRIVATE (self);

  /* free any data held directly by the object here */
#ifdef HAVE_CST
  if (priv->cst != NULL)
    CST_free(priv->cst);
  priv->cst = NULL;
#endif

  if (priv->ssl_ctx != NULL) 
    SSL_CTX_free(priv->ssl_ctx);
  priv->ssl_ctx = NULL;

  G_OBJECT_CLASS (gibber_ssl_transport_parent_class)->finalize (object);
}

static int
ssl_verify_cb (int preverify_ok, X509_STORE_CTX *x509_ctx)
{
  /* As this callback doesn't get auxiliary pointer parameter we
   * cannot really use this. However, we can retrieve results later. */
  DEBUG("Verification requested");
  return 1;
}


static gboolean
ssl_base_initialize(GibberSSLTransport *ssl, GError **error) 
{
  GibberSSLTransportPrivate *priv = GIBBER_SSL_TRANSPORT_GET_PRIVATE(ssl);
  static gboolean initialized = FALSE;
  /*const char *cert_file = NULL;*/

  if (!initialized) {
    SSL_library_init();
    SSL_load_error_strings();
    initialized = TRUE;
  }

  priv->ssl_method = TLSv1_client_method();
  if (priv->ssl_method == NULL) {
    fprintf(stderr, "TLSv1_client_method() == NULL\n");
    abort();
  }

  priv->ssl_ctx = SSL_CTX_new(priv->ssl_method);
  if (priv->ssl_ctx == NULL) {
   fprintf(stderr, "SSL_CTX_new() == NULL\n");
   abort();
  }
  /*if (access("/etc/ssl/cert.pem", R_OK) == 0)
    cert_file = "/etc/ssl/cert.pem";
  if (!SSL_CTX_load_verify_locations(ssl->ssl_ctx,
    cert_file, "/etc/ssl/certs")) {
    fprintf(stderr, "SSL_CTX_load_verify_locations() failed\n");
  }*/
  SSL_CTX_set_default_verify_paths(priv->ssl_ctx);
#ifdef HAVE_CST
  priv->cst = CST_open(0, NULL);
  if (priv->cst == NULL) {
    fprintf(stderr, "CST_open() == NULL\n");
    abort();
  }
  SSL_CTX_set_cert_verify_callback(ssl->ssl_ctx, ssl_cst_cb, ssl);
#endif
  SSL_CTX_set_verify(priv->ssl_ctx, SSL_VERIFY_PEER, ssl_verify_cb);
  return TRUE;
}

static gboolean
ssl_writeout(GibberSSLTransport *ssl, GError **error) {
  GibberSSLTransportPrivate *priv = GIBBER_SSL_TRANSPORT_GET_PRIVATE(ssl);
  long output;
  char *pp;
  gboolean ret = TRUE;

  output = BIO_get_mem_data(priv->wbio, &pp); 
  DEBUG("%ld bytes in output bio", output);
  if (output > 0 ) {
    int discard;
    ret = gibber_transport_send(priv->transport, (guint8 *)pp, output, error);
    discard = BIO_reset(priv->wbio);
  }
  return ret;
}

static void
ssl_resume_connect(GibberSSLTransport *ssl) 
{
  GibberSSLTransportPrivate *priv = GIBBER_SSL_TRANSPORT_GET_PRIVATE(ssl);
  gint ret;

  ret = SSL_connect(priv->ssl);

  if (ret < 0) {
    int error;
    ERR_print_errors_fp(stderr);
    switch ((error = SSL_get_error(priv->ssl, ret))) {
      case SSL_ERROR_WANT_READ:
         DEBUG("More data needed for ssl connection");
         break;
      default:
         DEBUG("Unhandled error: %d", error); 
         g_assert_not_reached();
    }
  } else if (ret == 1) {
    DEBUG("SSL Connection made");
    gibber_transport_set_state(GIBBER_TRANSPORT(ssl), 
                               GIBBER_TRANSPORT_CONNECTED);
  }
  ssl_writeout(ssl, NULL);
}


#define BUFSIZE 1024
static void
ssl_read_input(GibberSSLTransport *ssl) 
{
  GibberSSLTransportPrivate *priv = GIBBER_SSL_TRANSPORT_GET_PRIVATE(ssl);
  guint8 buffer[1024];
  gint len;

  while (BIO_ctrl_pending(priv->rbio) > 0) {
    len = SSL_read(priv->ssl, buffer, BUFSIZE); 
    g_assert(len > 0);
    gibber_transport_received_data(GIBBER_TRANSPORT(ssl), buffer, len);
  }
}

static void
ssl_handle_read(GibberTransport *transport, GibberBuffer *buffer, 
    gpointer user_data) 
{
  GibberSSLTransport *ssl = GIBBER_SSL_TRANSPORT(user_data);
  GibberSSLTransportPrivate *priv = GIBBER_SSL_TRANSPORT_GET_PRIVATE(ssl);
  int ret;

  DEBUG("Adding %zd bytes to read BIO", buffer->length);
  ret = BIO_write(priv->rbio, buffer->data, buffer->length);
  g_assert(ret == buffer->length);

  switch (gibber_transport_get_state(GIBBER_TRANSPORT(ssl))) {
    case GIBBER_TRANSPORT_CONNECTING:
      ssl_resume_connect(ssl);
      break;
    case GIBBER_TRANSPORT_CONNECTED:
      ssl_read_input(ssl);
      break;
    default: 
      g_assert_not_reached();
      break;
  }
}


GibberSSLTransport *
gibber_ssl_transport_new(GibberTransport *transport) {
  GibberSSLTransport *ssl;
  GibberSSLTransportPrivate *priv;
  
  ssl = g_object_new(GIBBER_TYPE_SSL_TRANSPORT, NULL);

  priv = GIBBER_SSL_TRANSPORT_GET_PRIVATE (ssl);
  priv->transport = g_object_ref(transport);

  gibber_transport_set_handler(priv->transport, ssl_handle_read, ssl);

  /* FIXME catch errors */
  ssl_base_initialize(ssl, NULL);

  return ssl;
}


gboolean
gibber_ssl_transport_connect(GibberSSLTransport *ssl, 
                             const gchar *server,
                             GError **error) {
  GibberSSLTransportPrivate *priv = GIBBER_SSL_TRANSPORT_GET_PRIVATE(ssl);

  g_assert(gibber_transport_get_state(priv->transport) ==
           GIBBER_TRANSPORT_CONNECTED);

  DEBUG("Starting ssl connection");

  priv->ssl = SSL_new(priv->ssl_ctx);
  if (priv->ssl == NULL) {
    fprintf(stderr, "SSL_new() == NULL\n");
    g_set_error(error, GIBBER_SSL_TRANSPORT_ERROR, 
      GIBBER_SSL_TRANSPORT_ERROR_CONNECTION_OPEN,
      "SSL_new()");
    return FALSE;
  }

  priv->rbio = BIO_new(BIO_s_mem());
  priv->wbio = BIO_new(BIO_s_mem());
  //priv->bio = BIO_new_socket(GIBBER_FD_TRANSPORT(priv->transport)->fd,
   //                          BIO_NOCLOSE);
  if (priv->rbio == NULL || priv->wbio == NULL) {
    fprintf(stderr, "BIO_new_socket() failed\n");
    g_set_error(error, GIBBER_SSL_TRANSPORT_ERROR, 
        GIBBER_SSL_TRANSPORT_ERROR_CONNECTION_OPEN,
        "BIO_new_socket()");
    return FALSE;
  }

  BIO_set_mem_eof_return(priv->rbio, -1);

  SSL_set_bio(priv->ssl, priv->rbio, priv->wbio);

  gibber_transport_set_state(GIBBER_TRANSPORT(ssl), 
      GIBBER_TRANSPORT_CONNECTING);

  ssl_resume_connect(ssl);

  /* Startup connection */
  return TRUE; 
}

static gboolean
ssl_transport_send(GibberTransport *transport, 
                        const guint8 *data, gsize size,
                        GError **error) {
  GibberSSLTransport *ssl = GIBBER_SSL_TRANSPORT(transport);
  GibberSSLTransportPrivate *priv = GIBBER_SSL_TRANSPORT_GET_PRIVATE(ssl);
  int ret;

  ret = SSL_write(priv->ssl, data, size);
  g_assert(ret > 0);
  return ssl_writeout(ssl, error);
}

static void
ssl_transport_disconnect(GibberTransport *transport) {
  GibberSSLTransport *ssl = GIBBER_SSL_TRANSPORT(transport);
  GibberSSLTransportPrivate *priv = GIBBER_SSL_TRANSPORT_GET_PRIVATE(ssl);

  gibber_transport_set_state(transport, GIBBER_TRANSPORT_DISCONNECTED);
  gibber_transport_disconnect(priv->transport);
  g_object_unref(priv->transport);
  priv->transport = NULL;
}

#if 0
int ssl_cst_cb (X509_STORE_CTX *x509_ctx, void *arg);
int ssl_verify_cb (int preverify_ok, X509_STORE_CTX *x509_ctx);

static gboolean ssl_verify_certificate (LmSSL *ssl, const gchar *server);
static GIOStatus ssl_io_status_from_return (LmSSL *ssl, gint error);

/*static char _ssl_error_code[11];*/

static void
ssl_print_state (LmSSL *ssl, const char *func, int val)
{
	unsigned long errid;
	const char *errmsg;

	switch (SSL_get_error(ssl->ssl, val)) {
		case SSL_ERROR_NONE:
			fprintf(stderr,
				"%s(): %i / SSL_ERROR_NONE\n",
				func, val);
			break;
		case SSL_ERROR_ZERO_RETURN:
			fprintf(stderr,
				"%s(): %i / SSL_ERROR_ZERO_RETURN\n",
				func, val);
			break;
		case SSL_ERROR_WANT_READ:
			fprintf(stderr,
				"%s(): %i / SSL_ERROR_WANT_READ\n",
				func, val);
			break;
		case SSL_ERROR_WANT_WRITE:
			fprintf(stderr,
				"%s(): %i / SSL_ERROR_WANT_WRITE\n",
				func, val);
			break;
		case SSL_ERROR_WANT_X509_LOOKUP:
			fprintf(stderr,
				"%s(): %i / SSL_ERROR_WANT_X509_LOOKUP\n",
				func, val);
			break;
		case SSL_ERROR_SYSCALL:
			fprintf(stderr,
				"%s(): %i / SSL_ERROR_SYSCALL\n",
				func, val);
			break;
		case SSL_ERROR_SSL:
			fprintf(stderr,
				"%s(): %i / SSL_ERROR_SSL\n",
				func, val);
			break;
	}
	do {
		errid = ERR_get_error();
		if (errid) {
			errmsg = ERR_error_string(errid, NULL);
			fprintf(stderr, "\t%s\n", errmsg);
		}
	} while (errid != 0);
}

/*static const char *
ssl_get_x509_err (long verify_res)
{
	sprintf(_ssl_error_code, "%ld", verify_res);
	return _ssl_error_code;
}*/

#ifdef HAVE_CST
int
ssl_cst_cb (X509_STORE_CTX *x509_ctx, void *arg)
{
	int cst_ec;
	int cst_state;
	int cst_error;
	int retval = 1;
	LmSSLBase *base;
	LmSSL *ssl = (LmSSL *) arg;

	base = LM_SSL_BASE(ssl);

	cst_ec = CST_is_valid(ssl->cst, x509_ctx->cert);
	cst_error = CST_last_error();
	cst_state = CST_get_state(ssl->cst, x509_ctx->cert);
	if (!cst_ec) {
		if (cst_error == CST_ERROR_CERT_NOTFOUND) {
			if (base->func(ssl,
				LM_SSL_STATUS_NO_CERT_FOUND,
				base->func_data) !=
				LM_SSL_RESPONSE_CONTINUE) {
				retval = 0;
			}
		}
		switch (cst_state) {
			case CST_STATE_NOTVALID:
			case CST_STATE_REVOKED:
				if (base->func(ssl,
					LM_SSL_STATUS_UNTRUSTED_CERT,
					base->func_data) !=
					LM_SSL_RESPONSE_CONTINUE) {
					retval = 0;
				}
				break;
			case CST_STATE_EXPIRED:
				if (base->func(ssl,
					LM_SSL_STATUS_CERT_EXPIRED,
					base->func_data) !=
					LM_SSL_RESPONSE_CONTINUE) {
					retval = 0;
				}
				break;
		}
	}
	return retval;
}
#endif
	
int
ssl_verify_cb (int preverify_ok, X509_STORE_CTX *x509_ctx)
{
	/* As this callback doesn't get auxiliary pointer parameter we
	 * cannot really use this. However, we can retrieve results later. */
	return 1;
}

static gboolean
ssl_verify_certificate (LmSSL *ssl, const gchar *server)
{
	gboolean retval = TRUE;
	LmSSLBase *base;
	long verify_res;
	unsigned int digest_len;
	X509 *srv_crt;
#ifndef HAVE_CST
	gchar *cn;
	X509_NAME *crt_subj;
#else
	char *crt_domain;
#endif

	base = LM_SSL_BASE(ssl);

	fprintf(stderr, "%s: Cipher: %s/%s/%i\n",
		__FILE__,
		SSL_get_cipher_version(ssl->ssl),
		SSL_get_cipher_name(ssl->ssl),
		SSL_get_cipher_bits(ssl->ssl, NULL));
	verify_res = SSL_get_verify_result(ssl->ssl);
	srv_crt = SSL_get_peer_certificate(ssl->ssl);
	if (base->expected_fingerprint != NULL) {
		X509_digest(srv_crt, EVP_md5(), (guchar *) base->fingerprint,
			&digest_len);
		if (memcmp(base->expected_fingerprint, base->fingerprint,
			digest_len) != 0) {
			if (base->func(ssl,
				LM_SSL_STATUS_CERT_FINGERPRINT_MISMATCH,
				base->func_data) != LM_SSL_RESPONSE_CONTINUE) {
				return FALSE;
			}
		}
	}
	fprintf(stderr, "%s: SSL_get_verify_result() = %ld\n",
		__FILE__,
		verify_res);
	switch (verify_res) {
		case X509_V_OK:
			break;
		case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
			/* special case for self signed certificates? */
		case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
		case X509_V_ERR_UNABLE_TO_GET_CRL:
		case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
			if (base->func(ssl,
				LM_SSL_STATUS_NO_CERT_FOUND,
				base->func_data) != LM_SSL_RESPONSE_CONTINUE) {
				retval = FALSE;
			}
			break;
		case X509_V_ERR_INVALID_CA:
		case X509_V_ERR_CERT_UNTRUSTED:
		case X509_V_ERR_CERT_REVOKED:
			if (base->func(ssl,
				LM_SSL_STATUS_UNTRUSTED_CERT,
				base->func_data) != LM_SSL_RESPONSE_CONTINUE) {
				retval = FALSE;
			}
			break;
		case X509_V_ERR_CERT_NOT_YET_VALID:
		case X509_V_ERR_CRL_NOT_YET_VALID:
			if (base->func(ssl,
				LM_SSL_STATUS_CERT_NOT_ACTIVATED,
				base->func_data) != LM_SSL_RESPONSE_CONTINUE) {
				retval = FALSE;
			}
			break;
		case X509_V_ERR_CERT_HAS_EXPIRED:
		case X509_V_ERR_CRL_HAS_EXPIRED:
			if (base->func(ssl,
				LM_SSL_STATUS_CERT_EXPIRED,
				base->func_data) != LM_SSL_RESPONSE_CONTINUE) {
				retval = FALSE;
			}
			break;
		default:
			if (base->func(ssl, LM_SSL_STATUS_GENERIC_ERROR,
				base->func_data) != LM_SSL_RESPONSE_CONTINUE) {
				retval = FALSE;
			}
	}
	/*if (retval == FALSE) {
		g_set_error (error, GIBBER_SSL_TRANSPORT_ERROR, GIBBER_SSL_TRANSPORT_ERROR_CONNECTION_OPEN,
			ssl_get_x509_err(verify_res), NULL);
	}*/
#ifndef HAVE_CST
	crt_subj = X509_get_subject_name(srv_crt);
	cn = (gchar *) g_malloc0(LM_SSL_CN_MAX + 1);
	if (cn == NULL) {
		fprintf(stderr, "g_malloc0() out of memory @ %s:%d\n",
			__FILE__, __LINE__);
		abort();
	}
	if (X509_NAME_get_text_by_NID(crt_subj, NID_commonName, cn,
		LM_SSL_CN_MAX) > 0) {
	fprintf(stderr, "%s: server = '%s', cn = '%s'\n",
		__FILE__, server, cn);
		if (strncmp(server, cn, LM_SSL_CN_MAX) != 0) {
			if (base->func(ssl,
				LM_SSL_STATUS_CERT_HOSTNAME_MISMATCH,
				base->func_data) != LM_SSL_RESPONSE_CONTINUE) {
				retval = FALSE;
			}
		}
	} else {
		fprintf(stderr, "X509_NAME_get_text_by_NID() failed\n");
	}
	fprintf(stderr, "%s:\n\tIssuer: %s\n\tSubject: %s\n\tFor: %s\n",
		__FILE__,
		X509_NAME_oneline(X509_get_issuer_name(srv_crt), NULL, 0),
		X509_NAME_oneline(X509_get_subject_name(srv_crt), NULL, 0),
		cn);
	g_free(cn);
#else  /* HAVE_CST */
	crt_domain = CST_get_domain_name(srv_crt);
	fprintf(stderr, "%s: Issued for CN: %s\n", __FILE__, crt_domain);
	if (crt_domain != NULL) {
		if (strcmp(server, crt_domain) != 0) {
			if (base->func(ssl,
				LM_SSL_STATUS_CERT_HOSTNAME_MISMATCH,
				base->func_data) != LM_SSL_RESPONSE_CONTINUE) {
				retval = FALSE;
			}
		}
	} else {
		if (base->func(ssl,
			LM_SSL_STATUS_CERT_HOSTNAME_MISMATCH,
			base->func_data) != LM_SSL_RESPONSE_CONTINUE) {
			retval = FALSE;
		}
	}
#endif  /* HAVE_CST */
	
	return retval;
}

static GIOStatus
ssl_io_status_from_return (LmSSL *ssl, gint ret)
{
	gint      error;
	GIOStatus status;

	if (ret > 0) return G_IO_STATUS_NORMAL;

	error = SSL_get_error(ssl->ssl, ret);
	switch (error) {
		case SSL_ERROR_WANT_READ:
		case SSL_ERROR_WANT_WRITE:
			status = G_IO_STATUS_AGAIN;
			break;
		case SSL_ERROR_ZERO_RETURN:
			status = G_IO_STATUS_EOF;
			break;
		default:
			status = G_IO_STATUS_ERROR;
	}

	return status;
}


GIOStatus
_lm_ssl_read (LmSSL *ssl, gchar *buf, gint len, gsize *bytes_read)
{
	GIOStatus status;
	gint ssl_ret;

	*bytes_read = 0;
	ssl_ret = SSL_read(ssl->ssl, buf, len);
	status = ssl_io_status_from_return(ssl, ssl_ret);
	if (status == G_IO_STATUS_NORMAL) {
		*bytes_read = ssl_ret;
	}
	
	return status;
}

gint
_lm_ssl_send (LmSSL *ssl, const gchar *str, gint len)
{
	GIOStatus status;
	gint ssl_ret;

	do {
		ssl_ret = SSL_write(ssl->ssl, str, len);
		if (ssl_ret <= 0) {
			status = ssl_io_status_from_return(ssl, ssl_ret);
			if (status != G_IO_STATUS_AGAIN)
				return -1;
		}
	} while (ssl_ret <= 0);

	return ssl_ret;
}

void 
_lm_ssl_close (LmSSL *ssl)
{
	SSL_shutdown(ssl->ssl);
	SSL_free(ssl->ssl);
	ssl->ssl = NULL;
}

void
_lm_ssl_free (LmSSL *ssl)
{
#ifdef HAVE_CST
	if (ssl->cst != NULL)
		CST_free(ssl->cst);
	ssl->cst = NULL;
#endif

	SSL_CTX_free(ssl->ssl_ctx);
	ssl->ssl_ctx = NULL;

	_lm_ssl_base_free_fields (LM_SSL_BASE(ssl));
	g_free (ssl);
}

#endif
