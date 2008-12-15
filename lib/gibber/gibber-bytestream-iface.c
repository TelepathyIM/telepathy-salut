/*
 * bytestream-iface.c - Source for GibberBytestream interface
 * Copyright (C) 2007 Collabora Ltd.
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

#include "gibber-bytestream-iface.h"
#include "gibber-signals-marshal.h"

#include <glib.h>

gboolean
gibber_bytestream_iface_initiate (GibberBytestreamIface *self)
{
  gboolean (*virtual_method)(GibberBytestreamIface *) =
    GIBBER_BYTESTREAM_IFACE_GET_CLASS (self)->initiate;
  g_assert (virtual_method != NULL);
  return virtual_method (self);
}

gboolean
gibber_bytestream_iface_send (GibberBytestreamIface *self,
                              guint len,
                              const gchar *data)
{
  gboolean (*virtual_method)(GibberBytestreamIface *, guint, const gchar *) =
    GIBBER_BYTESTREAM_IFACE_GET_CLASS (self)->send;
  g_assert (virtual_method != NULL);
  return virtual_method (self, len, data);
}

void
gibber_bytestream_iface_close (GibberBytestreamIface *self,
                               GError *error)
{
  void (*virtual_method)(GibberBytestreamIface *, GError *error) =
    GIBBER_BYTESTREAM_IFACE_GET_CLASS (self)->close;
  g_assert (virtual_method != NULL);
  virtual_method (self, error);
}

void
gibber_bytestream_iface_accept (GibberBytestreamIface *self,
                                GibberBytestreamAugmentSiAcceptReply func,
                                gpointer user_data)
{
  void (*virtual_method)(GibberBytestreamIface *,
      GibberBytestreamAugmentSiAcceptReply, gpointer) =
    GIBBER_BYTESTREAM_IFACE_GET_CLASS (self)->accept;
  g_assert (virtual_method != NULL);
  virtual_method (self, func, user_data);
}

void
gibber_bytestream_iface_block_read (GibberBytestreamIface *self,
                                    gboolean block)
{
  void (*virtual_method)(GibberBytestreamIface *, gboolean) =
    GIBBER_BYTESTREAM_IFACE_GET_CLASS (self)->block_read;
  if (virtual_method != NULL)
    virtual_method (self, block);
  /* else: do nothing. Some bytestreams like IBB does not have read_block. */
}

static void
gibber_bytestream_iface_base_init (gpointer klass)
{
  static gboolean initialized = FALSE;

  if (!initialized)
    {
      GParamSpec *param_spec;

     param_spec = g_param_spec_string (
          "self-id",
          "self ID",
          "the ID of the local user",
          "",
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

     param_spec = g_param_spec_string (
          "peer-id",
          "peer ID",
          "the ID of the muc or the remote user associated with this "
          "bytestream",
          "",
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_string (
          "stream-id",
          "stream ID",
          "the ID of the stream",
          "",
          G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

      param_spec = g_param_spec_uint (
          "state",
          "Bytestream state",
          "An enum (BytestreamIBBState) signifying the current state of"
          "this bytestream object",
          0, NUM_GIBBER_BYTESTREAM_STATES - 1,
          GIBBER_BYTESTREAM_STATE_LOCAL_PENDING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

     param_spec = g_param_spec_string (
          "protocol",
          "protocol",
          "the name of the protocol implemented by this bytestream",
          NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
      g_object_interface_install_property (klass, param_spec);

      g_signal_new ("data-received",
          G_TYPE_FROM_INTERFACE (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
          0,
          NULL, NULL,
          _gibber_signals_marshal_VOID__STRING_POINTER,
          G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_POINTER);

      g_signal_new ("state-changed",
          G_TYPE_FROM_INTERFACE (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__UINT,
          G_TYPE_NONE, 1, G_TYPE_UINT);

      g_signal_new ("write-blocked",
          G_TYPE_FROM_INTERFACE (klass),
          G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
          0,
          NULL, NULL,
          g_cclosure_marshal_VOID__BOOLEAN,
          G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

      initialized = TRUE;
    }
}

GType
gibber_bytestream_iface_get_type (void)
{
  static GType type = 0;

  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GibberBytestreamIfaceClass),
      gibber_bytestream_iface_base_init,   /* base_init */
      NULL,   /* base_finalize */
      NULL,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      0,
      0,      /* n_preallocs */
      NULL    /* instance_init */
    };

    type = g_type_register_static (G_TYPE_INTERFACE, "GibberBytestreamIface",
        &info, 0);
  }

  return type;
}
