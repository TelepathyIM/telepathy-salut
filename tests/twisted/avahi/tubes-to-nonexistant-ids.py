"""
Test that requests for Tubes and StreamTube channels to ids which aren't
actually on the network fail gracefully with NotAvailable
"""

from saluttest import exec_test

from constants import (
    HT_CONTACT, CONN_IFACE_REQUESTS,
    CHANNEL_TYPE, TARGET_HANDLE_TYPE, TARGET_HANDLE,
    CHANNEL_TYPE_TUBES, CHANNEL_TYPE_STREAM_TUBE,
    NOT_AVAILABLE
    )

import dbus

arbitrary_ids = [ "DooN4Bei@TheeK6bo-Tegh4aci", "ahrui1iM@Dai6igho-ADetaes3" ]

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    h1, h2 = conn.RequestHandles(HT_CONTACT, arbitrary_ids)

    try:
        conn.RequestChannel(CHANNEL_TYPE_TUBES, HT_CONTACT, h1, True)
    except dbus.DBusException, e:
        assert e.get_dbus_name() == NOT_AVAILABLE, e.get_dbus_name()
    else:
        assert False, "Should raise NotAvailable error"

    requestotron = dbus.Interface(conn, CONN_IFACE_REQUESTS)

    try:
        requestotron.CreateChannel({
            CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
            TARGET_HANDLE_TYPE: HT_CONTACT,
            TARGET_HANDLE: h2,
            CHANNEL_TYPE_STREAM_TUBE + ".Service": "com.example",
        })
    except dbus.DBusException, e:
        assert e.get_dbus_name() == NOT_AVAILABLE, e.get_dbus_name()
    else:
        assert False, "Should raise NotAvailable error"

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)
