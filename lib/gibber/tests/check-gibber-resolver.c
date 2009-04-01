/*
 * check-gibber-resolver.c - Test for gibber-resolver functions
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
#include <string.h>

#include "test-resolver.h"
#include "check-gibber.h"

#include <check.h>

GMainLoop *mainloop = NULL;
gboolean done = FALSE;

static void
resolver_srv_cb (GibberResolver *resolver, GList *srv_list, GError *error,
  gpointer user_data, GObject *weak_object)
{
  GList *s;
  int last_prio = 0;
  int last_weight = 0;

  for (s = srv_list ; s != NULL; s = g_list_next (s))
    {
      GibberResolverSrvRecord *r = (GibberResolverSrvRecord *) s->data;

      fail_unless (last_prio <= r->priority);

      if (last_prio != r->priority)
        last_weight = 0;

      /* If our previous weight was non-zero, then this one has to be non-zero
       * too. The SRV RFC requires all entries with zero weight to be sorted
       * before all other entries with the same priority */
      fail_unless (last_weight == 0 || r->weight != 0);

      last_prio = r->priority;
      last_weight = r->weight;
    }

  done = TRUE;

  if (g_main_loop_is_running (mainloop))
    g_main_loop_quit (mainloop);
}

START_TEST (test_srv_resolving)
{
  GibberResolver *resolver;

  done = FALSE;
  mainloop = g_main_loop_new (NULL, FALSE);

  resolver = g_object_new (TEST_TYPE_RESOLVER, NULL);

  gibber_resolver_srv (resolver, "test", "test",
    GIBBER_RESOLVER_SERVICE_TYPE_TCP,
    resolver_srv_cb, NULL, NULL, NULL);

  if (!done)
    g_main_loop_run (mainloop);

  g_main_loop_unref (mainloop);
} END_TEST

TCase *
make_gibber_resolver_tcase (void)
{
  TCase *tc = tcase_create ("Resolve");
  tcase_add_test (tc, test_srv_resolving);
  return tc;
}
