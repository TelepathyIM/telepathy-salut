/*
 * gibber-bytestream-muc.h - Header for GibberBytestreamMuc
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

#ifndef __GIBBER_BYTESTREAM_MUC_H__
#define __GIBBER_BYTESTREAM_MUC_H__

#include <glib-object.h>

#include "gibber-bytestream-iface.h"

G_BEGIN_DECLS

typedef struct _GibberBytestreamMuc GibberBytestreamMuc;
typedef struct _GibberBytestreamMucClass GibberBytestreamMucClass;

struct _GibberBytestreamMucClass {
  GObjectClass parent_class;
};

struct _GibberBytestreamMuc {
  GObject parent;

  gpointer priv;
};

GType gibber_bytestream_muc_get_type (void);

/* TYPE MACROS */
#define GIBBER_TYPE_BYTESTREAM_MUC \
  (gibber_bytestream_muc_get_type ())
#define GIBBER_BYTESTREAM_MUC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GIBBER_TYPE_BYTESTREAM_MUC,\
                              GibberBytestreamMuc))
#define GIBBER_BYTESTREAM_MUC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GIBBER_TYPE_BYTESTREAM_MUC,\
                           GibberBytestreamMucClass))
#define GIBBER_IS_BYTESTREAM_MUC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GIBBER_TYPE_BYTESTREAM_MUC))
#define GIBBER_IS_BYTESTREAM_MUC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GIBBER_TYPE_BYTESTREAM_MUC))
#define GIBBER_BYTESTREAM_MUC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GIBBER_TYPE_BYTESTREAM_MUC,\
                              GibberBytestreamMucClass))

void gibber_bytestream_muc_add_sender (GibberBytestreamMuc *bytestream,
    const gchar *sender, guint16 stream_id);

void gibber_bytestream_muc_remove_sender (GibberBytestreamMuc *bytestream,
    const gchar *sender);

G_END_DECLS

#endif /* #ifndef __GIBBER_BYTESTREAM_MUC_H__ */
