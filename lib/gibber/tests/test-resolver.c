/*
 * test-resolver.c - Source for TestResolver
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


#include <stdio.h>
#include <stdlib.h>

#include "test-resolver.h"

G_DEFINE_TYPE(TestResolver, test_resolver, GIBBER_TYPE_RESOLVER)

/* private structure */
typedef struct _TestResolverPrivate TestResolverPrivate;

struct _TestResolverPrivate
{
  gboolean dispose_has_run;
};

#define TEST_RESOLVER_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), TEST_TYPE_RESOLVER, TestResolverPrivate))

static void
test_resolver_init (TestResolver *obj)
{
}

static void test_resolver_dispose (GObject *object);
static void test_resolver_finalize (GObject *object);

static gboolean test_resolv_srv (GibberResolver *resolver, guint id,
  const gchar *service_name, const char *service,
  GibberResolverServiceType type);

static void
test_resolver_class_init (TestResolverClass *test_resolver_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (test_resolver_class);
  GibberResolverClass *resolver_class = GIBBER_RESOLVER_CLASS
    (test_resolver_class);

  g_type_class_add_private (test_resolver_class, sizeof (TestResolverPrivate));

  object_class->dispose = test_resolver_dispose;
  object_class->finalize = test_resolver_finalize;

  resolver_class->resolv_srv = test_resolv_srv;
}

void
test_resolver_dispose (GObject *object)
{
  TestResolver *self = TEST_RESOLVER (object);
  TestResolverPrivate *priv = TEST_RESOLVER_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (test_resolver_parent_class)->dispose)
    G_OBJECT_CLASS (test_resolver_parent_class)->dispose (object);
}

void
test_resolver_finalize (GObject *object)
{
  G_OBJECT_CLASS (test_resolver_parent_class)->finalize (object);
}


static gboolean test_resolv_srv (GibberResolver *resolver, guint id,
  const gchar *service_name, const char *service,
  GibberResolverServiceType type)
{
  GList *entries = NULL;
  int i;

  for (i = 0 ; i < 20 ; i++)
    {
      gchar *str;

      str = g_strdup_printf ("test%2d.example.com", i);

      entries = g_list_prepend (entries,
        gibber_resolver_srv_record_new (str, 1234,
          10 - (i / 5) , 4 - i % 5));

      g_free (str);
    }

  gibber_resolver_srv_result (resolver, id, entries, NULL);
  return FALSE;
}
