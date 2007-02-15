/*
 * gibber-xmpp-reader.h - Header for GibberXmppReader
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

#ifndef __GIBBER_XMPP_READER_H__
#define __GIBBER_XMPP_READER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GibberXmppReader GibberXmppReader;
typedef struct _GibberXmppReaderClass GibberXmppReaderClass;

struct _GibberXmppReaderClass {
    GObjectClass parent_class;
};

struct _GibberXmppReader {
    GObject parent;
};

GType gibber_xmpp_reader_get_type(void);

/* TYPE MACROS */
#define GIBBER_TYPE_XMPP_READER \
  (gibber_xmpp_reader_get_type())
#define GIBBER_XMPP_READER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_XMPP_READER, GibberXmppReader))
#define GIBBER_XMPP_READER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_XMPP_READER, GibberXmppReaderClass))
#define GIBBER_IS_XMPP_READER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_XMPP_READER))
#define GIBBER_IS_XMPP_READER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_XMPP_READER))
#define GIBBER_XMPP_READER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_XMPP_READER, GibberXmppReaderClass))


GibberXmppReader * gibber_xmpp_reader_new(void);
GibberXmppReader * gibber_xmpp_reader_new_no_stream(void);
void gibber_xmpp_reader_reset(GibberXmppReader *reader);

gboolean gibber_xmpp_reader_push(GibberXmppReader *reader, 
                                const guint8 *data, gsize length,
                                GError **error);

G_END_DECLS

#endif /* #ifndef __GIBBER_XMPP_READER_H__*/
