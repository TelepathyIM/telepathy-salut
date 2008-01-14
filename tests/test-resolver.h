/*
 * test-resolver.h - Header for TestResolver
 * Copyright (C) 2008 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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

#ifndef __TEST_RESOLVER_H__
#define __TEST_RESOLVER_H__

#include <glib-object.h>
#include <gibber/gibber-resolver.h>

G_BEGIN_DECLS

typedef struct _TestResolver TestResolver;
typedef struct _TestResolverClass TestResolverClass;

struct _TestResolverClass {
    GibberResolverClass parent_class;
};

struct _TestResolver {
    GibberResolver parent;
};

GType test_resolver_get_type(void);

/* TYPE MACROS */
#define TEST_TYPE_RESOLVER \
  (test_resolver_get_type())
#define TEST_RESOLVER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TEST_TYPE_RESOLVER, TestResolver))
#define TEST_RESOLVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TEST_TYPE_RESOLVER, TestResolverClass))
#define TEST_IS_RESOLVER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TEST_TYPE_RESOLVER))
#define TEST_IS_RESOLVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TEST_TYPE_RESOLVER))
#define TEST_RESOLVER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TEST_TYPE_RESOLVER, TestResolverClass))


G_END_DECLS

#endif /* #ifndef __TEST_RESOLVER_H__*/
