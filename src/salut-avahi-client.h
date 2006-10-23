/*
 * salut-avahi-client.h - Header for SalutAvahiClient
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

#ifndef __SALUT_AVAHI_CLIENT_H__
#define __SALUT_AVAHI_CLIENT_H__

#include <glib-object.h>
#include <avahi-client/client.h>

G_BEGIN_DECLS

typedef enum {
  SALUT_AVAHI_CLIENT_STATE_NOT_STARTED = -1,
  SALUT_AVAHI_CLIENT_STATE_S_REGISTERING = AVAHI_CLIENT_S_REGISTERING,
  SALUT_AVAHI_CLIENT_STATE_S_RUNNING = AVAHI_CLIENT_S_RUNNING,
  SALUT_AVAHI_CLIENT_STATE_S_COLLISION = AVAHI_CLIENT_S_COLLISION,
  SALUT_AVAHI_CLIENT_STATE_FAILURE = AVAHI_CLIENT_FAILURE,
  SALUT_AVAHI_CLIENT_STATE_CONNECTING = AVAHI_CLIENT_CONNECTING,
} SalutAvahiClientState;

typedef enum {
  SALUT_AVAHI_CLIENT_FLAG_NO_FLAGS = 0,
  SALUT_AVAHI_CLIENT_FLAG_IGNORE_USER_CONFIG = AVAHI_CLIENT_IGNORE_USER_CONFIG,
  SALUT_AVAHI_CLIENT_FLAG_NO_FAIL = AVAHI_CLIENT_NO_FAIL,
} SalutAvahiClientFlags;

typedef struct _SalutAvahiClient SalutAvahiClient;
typedef struct _SalutAvahiClientClass SalutAvahiClientClass;

struct _SalutAvahiClientClass {
    GObjectClass parent_class;
};

struct _SalutAvahiClient {
    GObject parent;
    /* Raw avahi_client handle, only reuse if you have reffed this instance */
    AvahiClient *avahi_client;
};

GType salut_avahi_client_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_AVAHI_CLIENT \
  (salut_avahi_client_get_type())
#define SALUT_AVAHI_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_AVAHI_CLIENT, SalutAvahiClient))
#define SALUT_AVAHI_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_AVAHI_CLIENT, SalutAvahiClientClass))
#define SALUT_IS_AVAHI_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_AVAHI_CLIENT))
#define SALUT_IS_AVAHI_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_AVAHI_CLIENT))
#define SALUT_AVAHI_CLIENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_AVAHI_CLIENT, SalutAvahiClientClass))

SalutAvahiClient *
salut_avahi_client_new(SalutAvahiClientFlags flags);

gboolean
salut_avahi_client_start(SalutAvahiClient *client, GError **error);


G_END_DECLS

#endif /* #ifndef __SALUT_AVAHI_CLIENT_H__*/
