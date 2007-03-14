/*
 * salut-muc-connection.h - Header for SalutMucConnection
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

#ifndef __SALUT_MUC_CONNECTION_H__
#define __SALUT_MUC_CONNECTION_H__

#include <glib-object.h>

#include <gibber/gibber-xmpp-connection.h>

G_BEGIN_DECLS

GQuark salut_muc_connection_error_quark (void);
#define SALUT_MUC_CONNECTION_ERROR \
  salut_muc_connection_error_quark()

typedef enum
{
  SALUT_MUC_CONNECTION_ERROR_INVALID_ADDRESS,
  SALUT_MUC_CONNECTION_ERROR_INVALID_PARAMETERS,
  SALUT_MUC_CONNECTION_ERROR_CONNECTION_FAILED,
} SalutMucConnectionError;


typedef struct _SalutMucConnection SalutMucConnection;
typedef struct _SalutMucConnectionClass SalutMucConnectionClass;

struct _SalutMucConnectionClass {
    GibberXmppConnectionClass parent_class;
};

struct _SalutMucConnection {
    GibberXmppConnection parent;
};

/* TYPE MACROS */
#define SALUT_TYPE_MUC_CONNECTION \
  (salut_muc_connection_get_type())
#define SALUT_MUC_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_MUC_CONNECTION, SalutMucConnection))
#define SALUT_MUC_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_MUC_CONNECTION, SalutMucConnectionClass))
#define SALUT_IS_MUC_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_MUC_CONNECTION))
#define SALUT_IS_MUC_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_MUC_CONNECTION))
#define SALUT_MUC_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_MUC_CONNECTION, SalutMucConnectionClass))

const gchar **
salut_muc_connection_get_protocols(void);

const gchar **
salut_muc_connection_get_required_parameters(const gchar *protocol);

SalutMucConnection *
salut_muc_connection_new(const gchar *name, 
                         const gchar *protocol,
                         GHashTable *parameters,
                         GError **error);

gboolean 
salut_muc_connection_connect(SalutMucConnection *connection, GError **error);

const gchar *
salut_muc_connection_get_protocol(SalutMucConnection *connection);

/* Current parameters of the transport. str -> str */
const GHashTable *
salut_muc_connection_get_parameters(SalutMucConnection *connection);

GType salut_muc_connection_get_type(void);

G_END_DECLS

#endif /* #ifndef __SALUT_MUC_CONNECTION_H__*/
