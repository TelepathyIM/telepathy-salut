#include <glib-object.h>

typedef struct _TestPluginClass TestPluginClass;
typedef struct _TestPlugin TestPlugin;

struct _TestPluginClass
{
  GObjectClass parent;
};

struct _TestPlugin
{
  GObject parent;
};

GType test_plugin_get_type (void);

#define TEST_TYPE_PLUGIN \
  (test_plugin_get_type ())
#define TEST_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TEST_TYPE_PLUGIN, TestPlugin))
#define TEST_PLUGIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), TEST_TYPE_PLUGIN, \
                           TestPluginClass))
#define TEST_IS_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), TEST_TYPE_PLUGIN))
#define TEST_IS_PLUGIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TEST_TYPE_PLUGIN))
#define TEST_PLUGIN_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TEST_TYPE_PLUGIN, \
                              TestPluginClass))

