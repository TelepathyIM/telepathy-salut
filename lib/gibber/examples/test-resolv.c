#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <glib.h>

#include <gibber/gibber-resolver.h>

GMainLoop *mainloop;
const gchar *hostname;
const gchar *servicename;

gboolean done = FALSE;
int to_resolve = 0;

GibberResolver *resolver;

static void
resolver_addrinfo_cb (GibberResolver *resolver, GList *entries, GError *error,
  gpointer user_data, GObject *weak_object)
{
  GList *e;
  GibberResolverSrvRecord *r = (GibberResolverSrvRecord *) user_data;

  printf ("-- %s %d:\n", r->hostname, r->port);
  if (error != NULL)
    {
      printf ("\tResolving failed: %s\n", error->message);
      goto out;
    }

  for (e = entries; e != NULL; e = e->next)
    {
      GibberResolverAddrInfo *addr = (GibberResolverAddrInfo *)e->data;
      gchar *hostname, *portname;

      g_assert (gibber_resolver_sockaddr_to_str (
        (struct sockaddr *) &(addr->sockaddr), addr->sockaddr_len,
        &hostname, &portname, NULL));

      printf ("\t %s %s\n", hostname, portname);

      g_free (hostname);
      g_free (portname);
    }

  gibber_resolver_addrinfo_list_free (entries);

out:
  to_resolve--;
  if (to_resolve == 0)
    {
      done = TRUE;
      if (g_main_loop_is_running (mainloop))
        g_main_loop_quit (mainloop);
    }
}

static void
resolver_srv_cb (GibberResolver *resolver, GList *srv_list, GError *error,
  gpointer user_data, GObject *weak_object)
{
  GList *s;

  printf ("--Srv returned--\n");
  if (error != NULL)
    {
      printf ("An error occured: %s\n", error->message);
      goto failed;
    }

  if (srv_list == NULL)
    {
      printf ("No srv records\n");
      goto failed;
    }

  for (s = srv_list ; s != NULL; s = s->next)
    {
      GibberResolverSrvRecord *r = (GibberResolverSrvRecord *) s->data;
      printf ("\t* %s\t%d\tp: %d w: %d\n", r->hostname, r->port,
        r->priority, r->weight);
    }

  printf ("Resolving individual records\n");

  to_resolve = g_list_length (srv_list);
  for (s = srv_list ; s != NULL; s = g_list_delete_link (s, s))
    {
      GibberResolverSrvRecord *r = (GibberResolverSrvRecord *) s->data;
      gchar *portname = g_strdup_printf("%d", r->port);

      gibber_resolver_addrinfo (resolver,
        r->hostname, portname, AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, 0,
        resolver_addrinfo_cb, r, (GDestroyNotify) gibber_resolver_srv_free,
        NULL);
    }

  return;

failed:
  done = TRUE;
}

int
main(int argc, char **argv) {
  g_type_init();

  g_assert (argc > 2);

  hostname = argv[1];
  servicename = argv[2];

  mainloop = g_main_loop_new(NULL, FALSE);

  resolver = gibber_resolver_get_resolver ();

  gibber_resolver_srv (resolver, hostname, servicename,
    GIBBER_RESOLVER_SERVICE_TYPE_TCP,
    resolver_srv_cb, NULL, NULL, NULL);

  if (!done)
    g_main_loop_run(mainloop);

  return 0;
}
