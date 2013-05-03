/*
 * muc-tube-stream.c - Source for SalutMucTubeStream
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

#include "config.h"

#include "muc-tube-stream.h"

G_DEFINE_TYPE (SalutMucTubeStream, salut_muc_tube_stream,
    SALUT_TYPE_TUBE_STREAM)

static void
salut_muc_tube_stream_init (SalutMucTubeStream *self)
{
}

static GPtrArray *
salut_muc_tube_stream_get_interfaces (TpBaseChannel *chan)
{
  GPtrArray *interfaces = TP_BASE_CHANNEL_CLASS (salut_muc_tube_stream_parent_class)
    ->get_interfaces (chan);

  g_ptr_array_add (interfaces, TP_IFACE_CHANNEL_INTERFACE_GROUP);
  return interfaces;
}

static void
salut_muc_tube_stream_class_init (
    SalutMucTubeStreamClass *salut_muc_tube_stream_class)
{
  TpBaseChannelClass *base_class = TP_BASE_CHANNEL_CLASS (
      salut_muc_tube_stream_class);

  base_class->get_interfaces = salut_muc_tube_stream_get_interfaces;
  base_class->target_handle_type = TP_HANDLE_TYPE_ROOM;
}
