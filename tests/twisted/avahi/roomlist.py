
"""
Test room list support.
"""

import dbus
import avahitest

from twisted.words.xish import domish

from saluttest import exec_test
from servicetest import call_async, EventPattern, \
        tp_name_prefix, tp_path_prefix, wrap_channel
import constants as cs

def test(q, bus, conn):
    self_name = 'testsuite' + '@' + avahitest.get_host_name()

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    # check if we can request roomlist channels
    properties = conn.GetAll(cs.CONN,
            dbus_interface='org.freedesktop.DBus.Properties')

    assert ({tp_name_prefix + '.Channel.ChannelType':
                cs.CHANNEL_TYPE_ROOM_LIST,
             tp_name_prefix + '.Channel.TargetHandleType': 0,
             },
             [],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    requestotron = dbus.Interface(conn,
            tp_name_prefix + '.Connection.Interface.Requests')

    # create roomlist channel using new API
    call_async(q, requestotron, 'CreateChannel',
            { tp_name_prefix + '.Channel.ChannelType':
                cs.CHANNEL_TYPE_ROOM_LIST,
              tp_name_prefix + '.Channel.TargetHandleType': 0,
              })

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        )
    path2 = ret.value[0]
    chan2 = wrap_channel(bus.get_object(conn.bus_name, path2), "RoomList1")

    props = ret.value[1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            cs.CHANNEL_TYPE_ROOM_LIST
    assert props[tp_name_prefix + '.Channel.TargetHandleType'] == 0
    assert props[tp_name_prefix + '.Channel.TargetHandle'] == 0
    assert props[tp_name_prefix + '.Channel.TargetID'] == ''
    assert props[tp_name_prefix + '.Channel.Requested'] == True
    assert props[tp_name_prefix + '.Channel.InitiatorHandle'] \
            == conn.Properties.Get(cs.CONN, "SelfHandle")
    assert props[tp_name_prefix + '.Channel.InitiatorID'] \
            == self_name
    assert props[tp_name_prefix + '.Channel.Type.RoomList1.Server'] == ''

    assert new_sig.args[0] == path2
    assert new_sig.args[1] == props

    assert chan2.Properties.Get(cs.CHANNEL_TYPE_ROOM_LIST, 'Server') == ''

    # ensure roomlist channel
    yours, ensured_path, ensured_props = requestotron.EnsureChannel(
            { tp_name_prefix + '.Channel.ChannelType':
                cs.CHANNEL_TYPE_ROOM_LIST,
              tp_name_prefix + '.Channel.TargetHandleType': 0,
              })

    assert not yours
    assert ensured_path == path2, (ensured_path, path2)

    # Closing roomlist channels crashed Salut for a while.
    chan2.Close()
    q.expect_many(
            EventPattern('dbus-signal', signal='Closed',
                path=path2),
            EventPattern('dbus-signal', signal='ChannelClosed', args=[path2]),
            )

    conn.Disconnect()

    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]),
            )

if __name__ == '__main__':
    exec_test(test)

