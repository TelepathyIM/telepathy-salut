/*
 * salut-lm-connection.h - Header for SalutLmConnection
 * Copyright (C) 2006 Collabora Ltd.
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

#ifndef __SALUT_LM_CONNECTION_H__
#define __SALUT_LM_CONNECTION_H__

#include <loudmouth/loudmouth.h>
#include <glib-object.h>

#include <sys/socket.h>
#include <netdb.h>

G_BEGIN_DECLS

typedef enum {
  SALUT_LM_DISCONNECTED,
  SALUT_LM_CONNECTING,
  SALUT_LM_CONNECTED,
} SalutLmConnectionState;

typedef struct _SalutLmConnection SalutLmConnection;
typedef struct _SalutLmConnectionClass SalutLmConnectionClass;


struct _SalutLmConnectionClass {
    GObjectClass parent_class;
};

struct _SalutLmConnection {
    GObject parent;
    SalutLmConnectionState state;
};

GType salut_lm_connection_get_type(void);

/* TYPE MACROS */
#define SALUT_TYPE_LM_CONNECTION \
  (salut_lm_connection_get_type())
#define SALUT_LM_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), SALUT_TYPE_LM_CONNECTION, SalutLmConnection))
#define SALUT_LM_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), SALUT_TYPE_LM_CONNECTION, SalutLmConnectionClass))
#define SALUT_IS_LM_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), SALUT_TYPE_LM_CONNECTION))
#define SALUT_IS_LM_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), SALUT_TYPE_LM_CONNECTION))
#define SALUT_LM_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), SALUT_TYPE_LM_CONNECTION, SalutLmConnectionClass))

SalutLmConnection *
salut_lm_connection_new(void);

/* FIXME ugly unsymmetrica abi between open and _from */
SalutLmConnection *
salut_lm_connection_new_from_fd(int fd);

void
salut_lm_connection_fd_start(SalutLmConnection *connection);

gboolean
salut_lm_connection_is_incoming(SalutLmConnection *connection);

void
salut_lm_connection_set_incoming(SalutLmConnection *connetion,
                                 gboolean incoming);

gboolean
salut_lm_connection_open_sockaddr(SalutLmConnection *connection,
                                  struct sockaddr_storage *addr,
                                  GError **error);

gboolean
salut_lm_connection_send(SalutLmConnection *connection,
                         LmMessage *message,
                         GError **error);

gboolean
salut_lm_connection_get_address(SalutLmConnection *connection, 
                                struct sockaddr_storage *addr,
                                socklen_t *len);

void
salut_lm_connection_close(SalutLmConnection *connection);

G_END_DECLS

#endif /* #ifndef __SALUT_LM_CONNECTION_H__*/
