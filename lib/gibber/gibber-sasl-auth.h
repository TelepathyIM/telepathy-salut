/*
 * gibber-sasl-auth.h - Header for GibberSaslAuth
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

#ifndef __GIBBER_SASL_AUTH_H__
#define __GIBBER_SASL_AUTH_H__

#include <glib-object.h>

#include "gibber-xmpp-stanza.h"
#include "gibber-xmpp-connection.h"

G_BEGIN_DECLS

GQuark gibber_sasl_auth_error_quark (void);
#define GIBBER_SASL_AUTH_ERROR \
  gibber_sasl_auth_error_quark()

typedef enum
{
  /* Failed to initialize our sasl support */
  GIBBER_SASL_AUTH_ERROR_INIT_FAILED, 
  /* Server doesn't support sasl (no mechanisms) */
  GIBBER_SASL_AUTH_ERROR_SASL_NOT_SUPPORTED,
  /* Server doesn't support any mechanisms that we support */
  GIBBER_SASL_AUTH_ERROR_NO_SUPPORTED_MECHANISMS,
  /* Couldn't send our stanza's to the server */
  GIBBER_SASL_AUTH_ERROR_NETWORK,
  /* Server send an invalid reply */
  GIBBER_SASL_AUTH_ERROR_INVALID_REPLY,
  /* Failure to provide user credentials */
  GIBBER_SASL_AUTH_ERROR_NO_CREDENTIALS,
  /* Server send a failure */
  GIBBER_SASL_AUTH_ERROR_FAILURE,
} GibberSaslAuthError;

typedef struct _GibberSaslAuth GibberSaslAuth;
typedef struct _GibberSaslAuthClass GibberSaslAuthClass;

struct _GibberSaslAuthClass {
    GObjectClass parent_class;
};

struct _GibberSaslAuth {
    GObject parent;
};

GType gibber_sasl_auth_get_type(void);

/* TYPE MACROS */
#define GIBBER_TYPE_SASL_AUTH \
  (gibber_sasl_auth_get_type())
#define GIBBER_SASL_AUTH(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_SASL_AUTH, GibberSaslAuth))
#define GIBBER_SASL_AUTH_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_SASL_AUTH, GibberSaslAuthClass))
#define GIBBER_IS_SASL_AUTH(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_SASL_AUTH))
#define GIBBER_IS_SASL_AUTH_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_SASL_AUTH))
#define GIBBER_SASL_AUTH_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_SASL_AUTH, GibberSaslAuthClass))

GibberSaslAuth *gibber_sasl_auth_new(void);


/* Initiate sasl auth. features should containt the stream features stanza as
 * receiver from the server */ 
gboolean 
gibber_sasl_auth_authenticate(GibberSaslAuth *sasl,
                              const gchar *server,
                              GibberXmppConnection *connection,
                              GibberXmppStanza *features,
                              gboolean allow_plain,
                              GError **error);
G_END_DECLS

#endif /* #ifndef __GIBBER_SASL_AUTH_H__*/
