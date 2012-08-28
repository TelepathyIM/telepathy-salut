/*
 * muc-tube-stream.h - Header for SalutMucTubeStream
 * Copyright (C) 2012 Collabora Ltd.
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

#ifndef __SALUT_MUC_TUBE_STREAM_H__
#define __SALUT_MUC_TUBE_STREAM_H__

#include <glib-object.h>

#include "tube-stream.h"

G_BEGIN_DECLS

typedef struct _SalutMucTubeStream SalutMucTubeStream;
typedef struct _SalutMucTubeStreamClass SalutMucTubeStreamClass;

struct _SalutMucTubeStreamClass {
  SalutTubeStreamClass parent_class;
};

struct _SalutMucTubeStream {
  SalutTubeStream parent;
};

GType salut_muc_tube_stream_get_type (void);

/* TYPE MACROS */
#define SALUT_TYPE_MUC_TUBE_STREAM \
  (salut_muc_tube_stream_get_type ())
#define SALUT_MUC_TUBE_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_MUC_TUBE_STREAM, SalutMucTubeStream))
#define SALUT_MUC_TUBE_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_MUC_TUBE_STREAM,\
                           SalutMucTubeStreamClass))
#define SALUT_IS_MUC_TUBE_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_MUC_TUBE_STREAM))
#define SALUT_IS_MUC_TUBE_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_MUC_TUBE_STREAM))
#define SALUT_MUC_TUBE_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_MUC_TUBE_STREAM,\
                              SalutMucTubeStreamClass))

G_END_DECLS

#endif /* #ifndef __SALUT_MUC_TUBE_STREAM_H__ */
