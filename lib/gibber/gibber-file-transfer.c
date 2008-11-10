/*
 * gibber-file-transfer.c - Source for GibberFileTransfer
 * Copyright (C) 2007 Marco Barisione <marco@barisione.org>
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
#include <string.h>

#include "gibber-file-transfer.h"
#include "gibber-oob-file-transfer.h"

#define DEBUG_FLAG DEBUG_FILE_TRANSFER
#include "gibber-debug.h"

#include "signals-marshal.h"
#include "gibber-file-transfer-enumtypes.h"


G_DEFINE_TYPE(GibberFileTransfer, gibber_file_transfer, G_TYPE_OBJECT)

/* signal enum */
enum
{
  REMOTE_ACCEPTED,
  FINISHED,
  ERROR,
  TRANSFERRED_CHUNK,
  CANCELLED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* properties */
enum
{
  PROP_ID = 1,
  PROP_SELF_ID,
  PROP_PEER_ID,
  PROP_FILENAME,
  PROP_DIRECTION,
  PROP_CONNECTION,
  PROP_DESCRIPTION,
  PROP_CONTENT_TYPE,
  LAST_PROPERTY
};

/* private structure */
struct _GibberFileTransferPrivate
{
  GibberXmppConnection *connection;

  guint64 size;
};


GQuark
gibber_file_transfer_error_quark (void)
{
  static GQuark error_quark = 0;

  if (error_quark == 0)
    error_quark =
        g_quark_from_static_string ("gibber-file-transfer-error-quark");

  return error_quark;
}

static void
gibber_file_transfer_init (GibberFileTransfer *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GIBBER_TYPE_FILE_TRANSFER,
      GibberFileTransferPrivate);
}

static void
gibber_file_transfer_get_property (GObject *object,
                                   guint property_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
  GibberFileTransfer *self = GIBBER_FILE_TRANSFER (object);

  switch (property_id)
    {
      case PROP_ID:
        g_value_set_string (value, self->id);
        break;
      case PROP_SELF_ID:
        g_value_set_string (value, self->self_id);
        break;
      case PROP_PEER_ID:
        g_value_set_string (value, self->peer_id);
        break;
      case PROP_FILENAME:
        g_value_set_string (value, self->filename);
        break;
      case PROP_DIRECTION:
        g_value_set_enum (value, self->direction);
        break;
      case PROP_CONNECTION:
        g_value_set_object (value, self->priv->connection);
        break;
      case PROP_DESCRIPTION:
        g_value_set_string (value, self->description);
        break;
      case PROP_CONTENT_TYPE:
        g_value_set_string (value, self->content_type);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static gchar *
generate_id (void)
{
  static guint id_num = 0;

  return g_strdup_printf ("gibber-file-transfer-%d", id_num++);
}

static void received_stanza_cb (GibberXmppConnection *conn,
    GibberXmppStanza *stanza, gpointer user_data);

static void
gibber_file_transfer_set_property (GObject *object,
                                   guint property_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
  GibberFileTransfer *self = GIBBER_FILE_TRANSFER (object);

  switch (property_id)
    {
      case PROP_ID:
        self->id = g_value_dup_string (value);
        if (self->id == NULL)
          self->id = generate_id ();
        break;
      case PROP_SELF_ID:
        self->self_id = g_value_dup_string (value);
        if (self->self_id == NULL)
          g_critical ("'self-id' cannot be NULL");
        break;
      case PROP_PEER_ID:
        self->peer_id = g_value_dup_string (value);
        if (self->peer_id == NULL)
          g_critical ("'peer-id' cannot be NULL");
        break;
      case PROP_FILENAME:
        self->filename = g_value_dup_string (value);
        break;
      case PROP_DIRECTION:
        self->direction = g_value_get_enum (value);
        break;
      case PROP_CONNECTION:
        self->priv->connection = g_value_dup_object (value);
        if (self->priv->connection != NULL)
          g_signal_connect (self->priv->connection, "received-stanza",
              G_CALLBACK (received_stanza_cb), self);
        break;
      case PROP_DESCRIPTION:
        self->description = g_value_dup_string (value);
        break;
      case PROP_CONTENT_TYPE:
        self->content_type = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void gibber_file_transfer_finalize (GObject *object);
static void gibber_file_transfer_dispose (GObject *object);

static void
gibber_file_transfer_class_init (GibberFileTransferClass *gibber_file_transfer_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (gibber_file_transfer_class);
  GParamSpec *param_spec;

  g_type_class_add_private (gibber_file_transfer_class, sizeof (GibberFileTransferPrivate));

  object_class->dispose = gibber_file_transfer_dispose;
  object_class->finalize = gibber_file_transfer_finalize;

  object_class->get_property = gibber_file_transfer_get_property;
  object_class->set_property = gibber_file_transfer_set_property;

  param_spec = g_param_spec_string ("id", "ID for the file transfer",
      "The ID used tpo indentify the file transfer", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ID, param_spec);

  param_spec = g_param_spec_string ("self-id",
      "Self ID",
      "The ID that identifies the local user", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_SELF_ID, param_spec);

  param_spec = g_param_spec_string ("peer-id",
      "Peer ID",
      "The ID that identifies the remote user", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER_ID, param_spec);

  param_spec = g_param_spec_string ("filename",
      "File name",
      "The name of the transferred file", "new-file",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_FILENAME, param_spec);

  param_spec = g_param_spec_enum ("direction",
      "Direction", "File transfer direction",
      GIBBER_TYPE_FILE_TRANSFER_DIRECTION,
      GIBBER_FILE_TRANSFER_DIRECTION_OUTGOING,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_DIRECTION, param_spec);

  param_spec = g_param_spec_object ("connection",
      "GibberXmppConnection object", "Gibber Connection used to send stanzas",
      GIBBER_TYPE_XMPP_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_string ("description",
      "Description",
      "The description of the transferred file", "",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_DESCRIPTION, param_spec);

  param_spec = g_param_spec_string ("content-type",
      "Content type",
      "The content type of the transferred file", "application/octet-stream",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONTENT_TYPE, param_spec);

  signals[REMOTE_ACCEPTED] = g_signal_new ("remote-accepted",
      G_OBJECT_CLASS_TYPE (gibber_file_transfer_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  signals[FINISHED] = g_signal_new ("finished",
      G_OBJECT_CLASS_TYPE (gibber_file_transfer_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);

  signals[ERROR] = g_signal_new ("error",
      G_OBJECT_CLASS_TYPE (gibber_file_transfer_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0, NULL, NULL,
      _gibber_signals_marshal_VOID__UINT_INT_STRING,
      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_INT, G_TYPE_STRING);

  signals[TRANSFERRED_CHUNK] = g_signal_new ("transferred-chunk",
      G_OBJECT_CLASS_TYPE (gibber_file_transfer_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0, NULL, NULL,
      _gibber_signals_marshal_VOID__UINT64,
      G_TYPE_NONE, 1, G_TYPE_UINT64);

  signals[CANCELLED] = g_signal_new ("cancelled",
      G_OBJECT_CLASS_TYPE (gibber_file_transfer_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);
}

static void
gibber_file_transfer_dispose (GObject *object)
{
  GibberFileTransfer *self = GIBBER_FILE_TRANSFER (object);

  if (self->priv->connection != NULL)
    {
      g_object_unref (self->priv->connection);
      self->priv->connection = NULL;
    }

  G_OBJECT_CLASS (gibber_file_transfer_parent_class)->dispose (object);
}

static void
gibber_file_transfer_finalize (GObject *object)
{
  GibberFileTransfer *self = GIBBER_FILE_TRANSFER (object);

  g_free (self->id);
  g_free (self->self_id);
  g_free (self->peer_id);
  g_free (self->filename);
  g_free (self->description);
  g_free (self->content_type);

  G_OBJECT_CLASS (gibber_file_transfer_parent_class)->finalize (object);
}

static void
received_stanza_cb (GibberXmppConnection *conn,
                    GibberXmppStanza *stanza,
                    gpointer user_data)
{
  GibberFileTransfer *self = user_data;
  const gchar *id;

  id = gibber_xmpp_node_get_attribute (stanza->node, "id");
  if (id != NULL && strcmp (id, self->id) == 0)
    GIBBER_FILE_TRANSFER_GET_CLASS (self)->received_stanza (self, stanza);
}

gboolean
gibber_file_transfer_is_file_offer (GibberXmppStanza *stanza)
{
  /* FIXME put the known backends in a list and stop when the first one
   * can handle the stanza */
  return gibber_oob_file_transfer_is_file_offer (stanza);
}

GibberFileTransfer *
gibber_file_transfer_new_from_stanza_with_from (
    GibberXmppStanza *stanza,
    GibberXmppConnection *connection,
    const gchar *from)
{
  /* FIXME put the known backends in a list and stop when the first one
   * can handle the stanza */
  GibberFileTransfer *ft;

  ft = gibber_oob_file_transfer_new_from_stanza_with_from (stanza, connection,
      from);
  /* it's not possible to have an outgoing transfer created from
   * a stanza */
  g_assert (ft == NULL ||
      ft->direction == GIBBER_FILE_TRANSFER_DIRECTION_INCOMING);

  return ft;
}

GibberFileTransfer *
gibber_file_transfer_new_from_stanza (GibberXmppStanza *stanza,
                                      GibberXmppConnection *connection)
{
  const gchar *from;

  from = gibber_xmpp_node_get_attribute (stanza->node, "from");

  return gibber_file_transfer_new_from_stanza_with_from (stanza, connection,
      from);
}

void
gibber_file_transfer_offer (GibberFileTransfer *self)
{
  GibberFileTransferClass *cls = GIBBER_FILE_TRANSFER_GET_CLASS (self);

  g_return_if_fail (self->direction ==
      GIBBER_FILE_TRANSFER_DIRECTION_OUTGOING);

  cls->offer (self);
}

void
gibber_file_transfer_send (GibberFileTransfer *self,
                           GIOChannel *src)
{
  GibberFileTransferClass *cls = GIBBER_FILE_TRANSFER_GET_CLASS (self);

  g_return_if_fail (self->direction ==
      GIBBER_FILE_TRANSFER_DIRECTION_OUTGOING);

  g_io_channel_set_buffered (src, FALSE);
  cls->send (self, src);
}

void
gibber_file_transfer_receive (GibberFileTransfer *self,
                              GIOChannel *dest)
{
  GibberFileTransferClass *cls = GIBBER_FILE_TRANSFER_GET_CLASS (self);

  g_return_if_fail (self->direction ==
      GIBBER_FILE_TRANSFER_DIRECTION_INCOMING);

  g_io_channel_set_buffered (dest, FALSE);
  cls->receive (self, dest);
}

void
gibber_file_transfer_cancel (GibberFileTransfer *self,
                             guint error_code)
{
  GibberFileTransferClass *cls = GIBBER_FILE_TRANSFER_GET_CLASS (self);

  cls->cancel (self, error_code);
}

void
gibber_file_transfer_emit_error (GibberFileTransfer *self,
                                 GError *error)
{
  DEBUG("File transfer error: %s", error->message);
  g_signal_emit (self, signals[ERROR], 0, error->domain, error->code,
      error->message);
}

void
gibber_file_transfer_set_size (GibberFileTransfer *self,
                               guint64 size)
{
  self->priv->size = size;
}

guint64
gibber_file_transfer_get_size (GibberFileTransfer *self)
{
  return self->priv->size;
}

gboolean
gibber_file_transfer_send_stanza (GibberFileTransfer *self,
                                  GibberXmppStanza *stanza,
                                  GError **error)
{
  return gibber_xmpp_connection_send (self->priv->connection, stanza, error);
}
