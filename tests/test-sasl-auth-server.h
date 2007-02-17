/*
 * test-sasl-auth-server.h - Header for TestSaslAuthServer
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

#ifndef __TEST_SASL_AUTH_SERVER_H__
#define __TEST_SASL_AUTH_SERVER_H__

#include <glib-object.h>

#include <gibber/gibber-transport.h>

G_BEGIN_DECLS

typedef enum {
  SERVER_PROBLEM_NO_PROBLEM,
  SERVER_PROBLEM_NO_SASL,
  SERVER_PROBLEM_NO_MECHANISMS,
} ServerProblem;

typedef struct _TestSaslAuthServer TestSaslAuthServer;
typedef struct _TestSaslAuthServerClass TestSaslAuthServerClass;

struct _TestSaslAuthServerClass {
    GObjectClass parent_class;
};

struct _TestSaslAuthServer {
    GObject parent;
};

GType test_sasl_auth_server_get_type(void);

/* TYPE MACROS */
#define TEST_TYPE_SASL_AUTH_SERVER \
  (test_sasl_auth_server_get_type())
#define TEST_SASL_AUTH_SERVER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TEST_TYPE_SASL_AUTH_SERVER, TestSaslAuthServer))
#define TEST_SASL_AUTH_SERVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TEST_TYPE_SASL_AUTH_SERVER, TestSaslAuthServerClass))
#define TEST_IS_SASL_AUTH_SERVER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TEST_TYPE_SASL_AUTH_SERVER))
#define TEST_IS_SASL_AUTH_SERVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TEST_TYPE_SASL_AUTH_SERVER))
#define TEST_SASL_AUTH_SERVER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TEST_TYPE_SASL_AUTH_SERVER, TestSaslAuthServerClass))


TestSaslAuthServer *
test_sasl_auth_server_new(GibberTransport *transport, gchar *mech,
                          const gchar *user, const gchar *password,
                          ServerProblem problem);

G_END_DECLS

#endif /* #ifndef __TEST_SASL_AUTH_SERVER_H__*/
