"""
Test Salut's implementation of sidecars, using the test plugin.
"""

from servicetest import (
    call_async, EventPattern, assertEquals
    )
from saluttest import exec_test
import constants as cs
from config import PLUGINS_ENABLED

TEST_PLUGIN_IFACE = "org.freedesktop.Telepathy.Salut.Plugin.Test"

if not PLUGINS_ENABLED:
    print "NOTE: built without --enable-plugins, not testing plugins"
    print "      (but still testing failing calls to EnsureSidecar)"

def test(q, bus, conn):
    # Request a sidecar thate we support before we're connected; it should just
    # wait around until we're connected.
    call_async(q, conn.Sidecars1, 'EnsureSidecar', TEST_PLUGIN_IFACE)

    conn.Connect()

    if PLUGINS_ENABLED:
        # Now we're connected, the call we made earlier should return.
        path, props = q.expect('dbus-return', method='EnsureSidecar').value
        # This sidecar doesn't even implement get_immutable_properties; it
        # should just get the empty dict filled in for it.
        assertEquals({}, props)

        # We should get the same sidecar if we request it again
        path2, props2 = conn.Sidecars1.EnsureSidecar(TEST_PLUGIN_IFACE)
        assertEquals((path, props), (path2, props2))
    else:
        # Only now does it fail.
        q.expect('dbus-error', method='EnsureSidecar')

    # This is not a valid interface name
    call_async(q, conn.Sidecars1, 'EnsureSidecar', 'not an interface')
    q.expect('dbus-error', name=cs.INVALID_ARGUMENT)

    # The test plugin makes no reference to this interface.
    call_async(q, conn.Sidecars1, 'EnsureSidecar', 'unsupported.sidecar')
    q.expect('dbus-error', name=cs.NOT_IMPLEMENTED)

    call_async(q, conn, 'Disconnect')

    q.expect_many(
        EventPattern('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED]),
        )

    call_async(q, conn.Sidecars1, 'EnsureSidecar', 'zomg.what')
    # With older telepathy-glib this would be DISCONNECTED;
    # with newer telepathy-glib the Connection disappears from the bus
    # sooner, and you get UnknownMethod or something from dbus-glib.
    q.expect('dbus-error')

if __name__ == '__main__':
    exec_test(test)
