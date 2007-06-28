/*
 * gibber-bytestream-ibb.h - Header for GibberBytestreamIBB
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

#ifndef __GIBBER_BYTESTREAM_IBB_H__
#define __GIBBER_BYTESTREAM_IBB_H__

#include <glib-object.h>
#include "gibber-xmpp-stanza.h"

//#include <telepathy-glib/base-connection.h>

G_BEGIN_DECLS

typedef enum
{
  /* Received a SI request, response not yet sent */
  GIBBER_BYTESTREAM_IBB_STATE_LOCAL_PENDING = 0,
  /* We accepted SI request.
   * bytestream specific init steps not yet performed */
  GIBBER_BYTESTREAM_IBB_STATE_ACCEPTED,
  /* Remote contact accepted the SI request.
   * bytestream specific initiation started */
  GIBBER_BYTESTREAM_IBB_STATE_INITIATING,
  /* Bytestream open */
  GIBBER_BYTESTREAM_IBB_STATE_OPEN,
  GIBBER_BYTESTREAM_IBB_STATE_CLOSED,
  LAST_GIBBER_BYTESTREAM_IBB_STATE,
} GibberBytestreamIBBState;

typedef struct _GibberBytestreamIBB GibberBytestreamIBB;
typedef struct _GibberBytestreamIBBClass GibberBytestreamIBBClass;

struct _GibberBytestreamIBBClass {
  GObjectClass parent_class;
};

struct _GibberBytestreamIBB {
  GObject parent;

  gpointer priv;
};

GType gibber_bytestream_ibb_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_BYTESTREAM_IBB \
  (gibber_bytestream_ibb_get_type ())
#define GIBBER_BYTESTREAM_IBB(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_BYTESTREAM_IBB,\
                              GibberBytestreamIBB))
#define GIBBER_BYTESTREAM_IBB_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_BYTESTREAM_IBB,\
                           GibberBytestreamIBBClass))
#define GIBBER_IS_BYTESTREAM_IBB(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_BYTESTREAM_IBB))
#define GIBBER_IS_BYTESTREAM_IBB_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_BYTESTREAM_IBB))
#define GIBBER_BYTESTREAM_IBB_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_BYTESTREAM_IBB,\
                              GibberBytestreamIBBClass))

gboolean
gibber_bytestream_ibb_initiation (GibberBytestreamIBB *ibb);

gboolean
gibber_bytestream_ibb_send (GibberBytestreamIBB *ibb, guint len,
   gchar *str);

void
gibber_bytestream_ibb_close (GibberBytestreamIBB *ibb);

/*
GibberXmppStanza *
gibber_bytestream_ibb_make_accept_iq (GibberBytestreamIBB *ibb);
*/

void
gibber_bytestream_ibb_accept (GibberBytestreamIBB *ibb, GibberXmppStanza *msg);

G_END_DECLS

#endif /* #ifndef __GIBBER_BYTESTREAM_IBB_H__ */