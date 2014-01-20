
"""
Test requesting of text 1-1 channels using the old and new request API.
"""

import dbus

from saluttest import (exec_test, wait_for_contact_in_publish)
from servicetest import call_async, EventPattern, \
        tp_name_prefix, make_channel_proxy
from avahitest import get_host_name, AvahiAnnouncer
from xmppstream import setup_stream_listener
import constants as cs

def test(q, bus, conn):
    self_name = 'testsuite' + '@' + get_host_name()

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    basic_txt = { "txtvers": "1", "status": "avail" }
    contact_name = "test-request-im@" + get_host_name()
    listener, port = setup_stream_listener(q, contact_name)

    AvahiAnnouncer(contact_name, "_presence._tcp", port, basic_txt)

    handle = wait_for_contact_in_publish(q, bus, conn, contact_name)

    # check if we can request roomlist channels
    properties = conn.GetAll(cs.CONN,
            dbus_interface='org.freedesktop.DBus.Properties')
    assert ({tp_name_prefix + '.Channel.ChannelType':
                cs.CHANNEL_TYPE_TEXT,
             tp_name_prefix + '.Channel.TargetHandleType': cs.HT_CONTACT,
             },
             [tp_name_prefix + '.Channel.TargetHandle',
              tp_name_prefix + '.Channel.TargetID'],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # create muc channel
    requestotron = dbus.Interface(conn,
            tp_name_prefix + '.Connection.Interface.Requests')
    call_async(q, requestotron, 'CreateChannel',
            { tp_name_prefix + '.Channel.ChannelType':
                cs.CHANNEL_TYPE_TEXT,
              tp_name_prefix + '.Channel.TargetHandleType': cs.HT_CONTACT,
              tp_name_prefix + '.Channel.TargetID': contact_name,
              })

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        )
    path2 = ret.value[0]
    chan = make_channel_proxy(conn, path2, "Channel")

    props = ret.value[1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            cs.CHANNEL_TYPE_TEXT
    assert props[tp_name_prefix + '.Channel.TargetHandleType'] == cs.HT_CONTACT
    assert props[tp_name_prefix + '.Channel.TargetHandle'] == handle
    assert props[tp_name_prefix + '.Channel.TargetID'] == contact_name
    assert props[tp_name_prefix + '.Channel.Requested'] == True
    assert props[tp_name_prefix + '.Channel.InitiatorHandle'] \
            == conn.Properties.Get(cs.CONN, "SelfHandle")
    assert props[tp_name_prefix + '.Channel.InitiatorID'] \
            == self_name

    assert new_sig.args[0] == path2
    assert new_sig.args[1] == props

    # ensure roomlist channel
    yours, ensured_path, ensured_props = requestotron.EnsureChannel(
            { tp_name_prefix + '.Channel.ChannelType':
                cs.CHANNEL_TYPE_TEXT,
              tp_name_prefix + '.Channel.TargetHandleType': cs.HT_CONTACT,
              tp_name_prefix + '.Channel.TargetHandle': handle,
              })

    assert not yours
    assert ensured_path == path2, (ensured_path, path2)

    conn.Disconnect()

    q.expect_many(
            EventPattern('dbus-signal', signal='Closed',
                path=path2),
            EventPattern('dbus-signal', signal='ChannelClosed', args=[path2]),
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]),
            )

if __name__ == '__main__':
    exec_test(test)
