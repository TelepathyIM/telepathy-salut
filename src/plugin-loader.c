/*
 * plugin-loader.c — plugin support for telepathy-salut
 * Copyright © 2009-2011 Collabora Ltd.
 * Copyright © 2009 Nokia Corporation
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

#include "plugin-loader.h"

#include <glib.h>

#ifdef ENABLE_PLUGINS
# include <gmodule.h>
#endif

#include <telepathy-glib/util.h>

#define DEBUG_FLAG DEBUG_PLUGIN
#include "debug.h"
#include "salut/plugin.h"

G_DEFINE_TYPE(SalutPluginLoader,
    salut_plugin_loader,
    G_TYPE_OBJECT)

struct _SalutPluginLoaderPrivate
{
  GPtrArray *plugins;
};

#ifdef ENABLE_PLUGINS
static void
plugin_loader_try_to_load (
    SalutPluginLoader *self,
    const gchar *path)
{
  GModule *m = g_module_open (path, G_MODULE_BIND_LOCAL);
  gpointer func;
  SalutPluginCreateImpl create;
  SalutPlugin *plugin;

  if (m == NULL)
    {
      const gchar *e = g_module_error ();

      /* the errors often seem to be prefixed by the filename */
      if (g_str_has_prefix (e, path))
        DEBUG ("%s", e);
      else
        DEBUG ("%s: %s", path, e);

      return;
    }

  if (!g_module_symbol (m, "salut_plugin_create", &func))
    {
      DEBUG ("%s", g_module_error ());
      g_module_close (m);
      return;
    }

  /* We're about to try to instantiate an object. This installs the
   * class with the type system, so we should ensure that this
   * plug-in is never accidentally unloaded.
   */
  g_module_make_resident (m);

  /* Here goes nothing... */
  create = func;
  plugin = create ();

  if (plugin == NULL)
    {
      g_warning ("salut_plugin_create () failed for %s", path);
    }
  else
    {
      const gchar *version = salut_plugin_get_version (plugin);

      if (version == NULL)
        version = "(unspecified)";

      DEBUG ("loaded '%s' version %s (%s)",
          salut_plugin_get_name (plugin), version, path);

      g_ptr_array_add (self->priv->plugins, plugin);
    }
}

static void
salut_plugin_loader_probe (SalutPluginLoader *self)
{
  GError *error = NULL;
  const gchar *directory_name = g_getenv ("SALUT_PLUGIN_DIR");
  GDir *d;
  const gchar *file;

  if (!g_module_supported ())
    {
      DEBUG ("modules aren't supported on this platform.");
      return;
    }

  if (directory_name == NULL)
    directory_name = PLUGIN_DIR;

  DEBUG ("probing %s", directory_name);
  d = g_dir_open (directory_name, 0, &error);

  if (d == NULL)
    {
      DEBUG ("%s", error->message);
      g_error_free (error);
      return;
    }

  while ((file = g_dir_read_name (d)) != NULL)
    {
      gchar *path;

      if (!g_str_has_suffix (file, G_MODULE_SUFFIX))
        continue;

      path = g_build_filename (directory_name, file, NULL);
      plugin_loader_try_to_load (self, path);
      g_free (path);
    }

  g_dir_close (d);
}
#endif

static void
salut_plugin_loader_init (SalutPluginLoader *self)
{
  SalutPluginLoaderPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      SALUT_TYPE_PLUGIN_LOADER, SalutPluginLoaderPrivate);

  self->priv = priv;
  priv->plugins = g_ptr_array_new_with_free_func (g_object_unref);
}

static GObject *
salut_plugin_loader_constructor (
    GType type,
    guint n_props,
    GObjectConstructParam *props)
{
  static gpointer singleton = NULL;

  if (singleton == NULL)
    {
      singleton = G_OBJECT_CLASS (salut_plugin_loader_parent_class)->
          constructor (type, n_props, props);
      g_object_add_weak_pointer (G_OBJECT (singleton), &singleton);

      return singleton;
    }
  else
    {
      return g_object_ref (singleton);
    }
}

static void
salut_plugin_loader_constructed (GObject *object)
{
  SalutPluginLoader *self = SALUT_PLUGIN_LOADER (object);
  void (*chain_up) (GObject *) =
      G_OBJECT_CLASS (salut_plugin_loader_parent_class)->constructed;

  if (chain_up != NULL)
    chain_up (object);

#ifdef ENABLE_PLUGINS
  salut_plugin_loader_probe (self);
#else
  DEBUG ("built without plugin support, not actually loading anything");
  (void) self; /* silence unused variable warning. */
#endif
}

static void
salut_plugin_loader_finalize (GObject *object)
{
  SalutPluginLoader *self = SALUT_PLUGIN_LOADER (object);
  void (*chain_up) (GObject *) =
      G_OBJECT_CLASS (salut_plugin_loader_parent_class)->finalize;

  tp_clear_pointer (&self->priv->plugins, g_ptr_array_unref);

  if (chain_up != NULL)
    chain_up (object);
}

static void
salut_plugin_loader_class_init (SalutPluginLoaderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (SalutPluginLoaderPrivate));

  object_class->constructor = salut_plugin_loader_constructor;
  object_class->constructed = salut_plugin_loader_constructed;
  object_class->finalize = salut_plugin_loader_finalize;
}

SalutPluginLoader *
salut_plugin_loader_dup ()
{
  return g_object_new (SALUT_TYPE_PLUGIN_LOADER, NULL);
}
