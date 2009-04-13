
"""
Test requesting of muc text channels using the old and new request API.
"""

import dbus
import avahi

from saluttest import exec_test
from avahitest import AvahiListener, txt_get_key
import constants as cs

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_CONNECTED, cs.CSR_NONE_SPECIFIED])

    self_handle = conn.GetSelfHandle()
    self_handle_name = conn.InspectHandles(cs.HT_CONTACT, [self_handle])[0]

    AvahiListener(q).listen_for_service("_presence._tcp")
    e = q.expect('service-added', name = self_handle_name, protocol = avahi.PROTO_INET)
    service = e.service

    service.resolve()

    def wait_for_presence_announce():
        e = q.expect('service-resolved', service=service)
        return txt_get_key(e.txt, 'status'), txt_get_key(e.txt, 'msg')

    # initial presence is available
    status, msg = wait_for_presence_announce()
    assert status == 'avail', status
    assert msg is None, msg

    statuses = conn.Get(cs.CONN_IFACE_SIMPLE_PRESENCE, 'Statuses', dbus_interface=dbus.PROPERTIES_IFACE)
    assert 'available' in statuses
    assert 'dnd' in statuses
    assert 'away' in statuses


    simple_presence = dbus.Interface(conn, cs.CONN_IFACE_SIMPLE_PRESENCE)
    # set your status to away
    simple_presence.SetPresence('away', 'At the pub')

    status, msg = wait_for_presence_announce()
    assert status == 'away', status
    assert msg == 'At the pub', msg

    # set your status to available without msg
    simple_presence.SetPresence('available', '')

    status, msg = wait_for_presence_announce()
    assert status == 'avail', status
    assert msg is None, msg

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED])

if __name__ == '__main__':
    exec_test(test)

