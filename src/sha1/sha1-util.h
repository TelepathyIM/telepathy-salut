/*
 * sha1-util.h - Header for sha1-utils
 * Copyright (C) 2006-2007 Collabora Ltd.
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

#ifndef __SALUT_SHA1_UTIL__
#define __SALUT_SHA1_UTIL__

#include <glib.h>

/* Guarantees that the resulting hash is in lower-case */
gchar *sha1_hex (const guint8 *bytes, guint len);

/* A SHA1 digest is 20 bytes long */
#define SHA1_HASH_SIZE 20
void sha1_bin (const gchar *bytes, guint len, guchar out[SHA1_HASH_SIZE]);

#endif /* #ifndef __SALUT_SHA1_UTIL__ */
