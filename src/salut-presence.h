/*
 * salut-presence.h - Header for Salut Presence types
 * Copyright (C) 2005 Collabora Ltd.
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

#ifndef __SALUT_PRESENCE_H__
#define __SALUT_PRESENCE_H__

#include <glib-object.h>
#include "telepathy-constants.h"

G_BEGIN_DECLS


/* private structure */
typedef struct {
  const gchar *name;
  const gchar *txt_name;
  TpConnectionPresenceType presence_type;
} SalutPresenceStatusInfo;


typedef enum {
  SALUT_PRESENCE_AVAILABLE,
  SALUT_PRESENCE_AWAY,
  SALUT_PRESENCE_DND,
  SALUT_PRESENCE_OFFLINE, /* offline is a dummy, FIXME, check handling */
  SALUT_PRESENCE_NR_PRESENCES
} SalutPresenceId;  

static const SalutPresenceStatusInfo salut_presence_statuses[] = {
  { "available", "avail",   TP_CONN_PRESENCE_TYPE_AVAILABLE },
  { "away",      "away",    TP_CONN_PRESENCE_TYPE_AWAY  },
  { "dnd",       "dnd",     TP_CONN_PRESENCE_TYPE_AWAY  },
  { NULL, NULL, 0}
};

G_END_DECLS

#endif /* #ifndef __SALUT_PRESENCE_H__*/
