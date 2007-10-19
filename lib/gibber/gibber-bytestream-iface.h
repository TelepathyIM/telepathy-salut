/*
 * bytestream-iface.h - Header for GibberBytestream interface
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

#ifndef __GIBBER_BYTESTREAM_IFACE_H__
#define __GIBBER_BYTESTREAM_IFACE_H__

#include <glib-object.h>

#include "gibber-xmpp-stanza.h"

G_BEGIN_DECLS

typedef enum
{
  /* Received a SI request, response not yet sent */
  GIBBER_BYTESTREAM_STATE_LOCAL_PENDING = 0,
  /* We accepted SI request.
   * bytestream specific init steps not yet performed */
  GIBBER_BYTESTREAM_STATE_ACCEPTED,
  /* Remote contact accepted the SI request.
   * bytestream specific initiation started */
  GIBBER_BYTESTREAM_STATE_INITIATING,
  /* Bytestream open */
  GIBBER_BYTESTREAM_STATE_OPEN,
  GIBBER_BYTESTREAM_STATE_CLOSED,
  NUM_GIBBER_BYTESTREAM_STATES,
} GibberBytestreamState;

typedef void (* GibberBytestreamAugmentSiAcceptReply) (
    GibberXmppNode *si, gpointer user_data);

typedef struct _GibberBytestreamIface GibberBytestreamIface;
typedef struct _GibberBytestreamIfaceClass GibberBytestreamIfaceClass;

struct _GibberBytestreamIfaceClass {
  GTypeInterface parent;

  gboolean (*initiate) (GibberBytestreamIface *bytestream);
  gboolean (*send) (GibberBytestreamIface *bytestream, guint len,
      const gchar *data);
  void (*close) (GibberBytestreamIface *bytestream, GError *error);
  void (*accept) (GibberBytestreamIface *bytestream,
      GibberBytestreamAugmentSiAcceptReply func, gpointer user_data);
  const gchar * (*get_protocol) (GibberBytestreamIface *bytestream);
};

GType gibber_bytestream_iface_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_BYTESTREAM_IFACE \
  (gibber_bytestream_iface_get_type ())
#define GIBBER_BYTESTREAM_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_BYTESTREAM_IFACE, \
                              GibberBytestreamIface))
#define GIBBER_IS_BYTESTREAM_IFACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_BYTESTREAM_IFACE))
#define GIBBER_BYTESTREAM_IFACE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GIBBER_TYPE_BYTESTREAM_IFACE,\
                              GibberBytestreamIfaceClass))

gboolean gibber_bytestream_iface_initiate (GibberBytestreamIface *bytestream);

gboolean gibber_bytestream_iface_send (GibberBytestreamIface *bytestream,
    guint len, const gchar *data);

void gibber_bytestream_iface_close (GibberBytestreamIface *bytestream,
    GError *error);

void gibber_bytestream_iface_accept (GibberBytestreamIface *bytestream,
    GibberBytestreamAugmentSiAcceptReply func, gpointer user_data);

const gchar * gibber_bytestream_iface_get_protocol (
    GibberBytestreamIface *bytestream);

G_END_DECLS

#endif /* #ifndef __GIBBER_BYTESTREAM_IFACE_H__ */
