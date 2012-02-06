/*
 * gibber-muc-connection.h - Header for GibberMucConnection
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

#ifndef __GIBBER_MUC_CONNECTION_H__
#define __GIBBER_MUC_CONNECTION_H__

#include <glib-object.h>

#include "gibber-r-multicast-transport.h"
#include <wocky/wocky.h>

G_BEGIN_DECLS

GQuark gibber_muc_connection_error_quark (void);
#define GIBBER_MUC_CONNECTION_ERROR \
  gibber_muc_connection_error_quark ()

typedef enum
{
  GIBBER_MUC_CONNECTION_ERROR_INVALID_ADDRESS,
  GIBBER_MUC_CONNECTION_ERROR_INVALID_PARAMETERS,
  GIBBER_MUC_CONNECTION_ERROR_INVALID_PROTOCOL,
  GIBBER_MUC_CONNECTION_ERROR_CONNECTION_FAILED,
} GibberMucConnectionError;

typedef enum
{
  GIBBER_MUC_CONNECTION_DISCONNECTED = 0,
  GIBBER_MUC_CONNECTION_CONNECTING,
  GIBBER_MUC_CONNECTION_CONNECTED,
  GIBBER_MUC_CONNECTION_DISCONNECTING,
} GibberMucConnectionState;


typedef struct _GibberMucConnection GibberMucConnection;
typedef struct _GibberMucConnectionClass GibberMucConnectionClass;

struct _GibberMucConnectionClass {
    GObjectClass parent_class;
};

struct _GibberMucConnection {
    GObject parent;
    GibberMucConnectionState state;
};

/* TYPE MACROS */
#define GIBBER_TYPE_MUC_CONNECTION \
  (gibber_muc_connection_get_type ())
#define GIBBER_MUC_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_MUC_CONNECTION, \
   GibberMucConnection))
#define GIBBER_MUC_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_MUC_CONNECTION, \
   GibberMucConnectionClass))
#define GIBBER_IS_MUC_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_MUC_CONNECTION))
#define GIBBER_IS_MUC_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_MUC_CONNECTION))
#define GIBBER_MUC_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_MUC_CONNECTION, \
   GibberMucConnectionClass))

const gchar ** gibber_muc_connection_get_protocols (void);

const gchar ** gibber_muc_connection_get_required_parameters (
    const gchar *protocol);

GibberMucConnection * gibber_muc_connection_new (const gchar *name,
    const gchar *protocol, GHashTable *parameters, GError **error);

gboolean gibber_muc_connection_connect (GibberMucConnection *connection,
    GError **error);

void gibber_muc_connection_disconnect (GibberMucConnection *connection);

const gchar * gibber_muc_connection_get_protocol (
    GibberMucConnection *connection);

/* Current parameters of the transport. str -> str */
const GHashTable * gibber_muc_connection_get_parameters (
    GibberMucConnection *connection);

GType gibber_muc_connection_get_type (void);

gboolean gibber_muc_connection_send (GibberMucConnection *connection,
    WockyStanza *stanza, GError **error);

gboolean
gibber_muc_connection_send_raw (GibberMucConnection *connection,
    guint16 stream_id, const guint8 *data, gsize size, GError **error);

guint16 gibber_muc_connection_new_stream (GibberMucConnection *connection);

void gibber_muc_connection_free_stream (GibberMucConnection *connection,
    guint16 stream_id);

G_END_DECLS

#endif /* #ifndef __GIBBER_MUC_CONNECTION_H__*/
