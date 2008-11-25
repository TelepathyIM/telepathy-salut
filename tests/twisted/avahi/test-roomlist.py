
"""
Test MUC support.
"""

import dbus
import avahitest

from twisted.words.xish import domish

from saluttest import exec_test
from servicetest import call_async, lazy, match, EventPattern, \
        tp_name_prefix, tp_path_prefix

def test(q, bus, conn):
    self_name = 'testsuite' + '@' + avahitest.get_host_name()

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    # check if we can request roomlist channels
    properties = conn.GetAll(
            tp_name_prefix + '.Connection.Interface.Requests',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert properties.get('Channels') == [], properties['Channels']
    assert ({tp_name_prefix + '.Channel.ChannelType':
                tp_name_prefix + '.Channel.Type.RoomList',
             tp_name_prefix + '.Channel.TargetHandleType': 0,
             },
             [],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    call_async(q, conn, 'RequestChannel',
        tp_name_prefix + '.Channel.Type.RoomList', 0, 0, True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    bus = dbus.SessionBus()
    path1 = ret.value[0]
    chan = bus.get_object(conn.bus_name, path1)

    assert new_sig.args[0][0][0] == path1

    props = new_sig.args[0][0][1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            tp_name_prefix + '.Channel.Type.RoomList'
    assert props[tp_name_prefix + '.Channel.TargetHandleType'] == 0
    assert props[tp_name_prefix + '.Channel.TargetHandle'] == 0
    assert props[tp_name_prefix + '.Channel.TargetID'] == ''
    assert props[tp_name_prefix + '.Channel.Requested'] == True
    assert props[tp_name_prefix + '.Channel.InitiatorHandle'] \
            == conn.GetSelfHandle()
    assert props[tp_name_prefix + '.Channel.InitiatorID'] \
            == self_name
    assert props[tp_name_prefix + '.Channel.Type.RoomList.Server'] == ''

    assert old_sig.args[0] == path1
    assert old_sig.args[1] == tp_name_prefix + '.Channel.Type.RoomList'
    assert old_sig.args[2] == 0     # handle type
    assert old_sig.args[3] == 0     # handle
    assert old_sig.args[4] == 1     # suppress handler

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = chan.GetAll(
            tp_name_prefix + '.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert channel_props.get('TargetHandle') == 0,\
            channel_props.get('TargetHandle')
    assert channel_props['TargetID'] == '', channel_props
    assert channel_props.get('TargetHandleType') == 0,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == \
            tp_name_prefix + '.Channel.Type.RoomList',\
            channel_props.get('ChannelType')
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == self_name
    assert channel_props['InitiatorHandle'] == conn.GetSelfHandle()

    assert chan.Get(
            tp_name_prefix + '.Channel.Type.RoomList', 'Server',
            dbus_interface='org.freedesktop.DBus.Properties') == ''

    # FIXME: actually list the rooms!

    requestotron = dbus.Interface(conn,
            tp_name_prefix + '.Connection.Interface.Requests')

    # create roomlist channel using new API
    call_async(q, requestotron, 'CreateChannel',
            { tp_name_prefix + '.Channel.ChannelType':
                tp_name_prefix + '.Channel.Type.RoomList',
              tp_name_prefix + '.Channel.TargetHandleType': 0,
              })

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    path2 = ret.value[0]
    chan = bus.get_object(conn.bus_name, path2)

    props = ret.value[1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            tp_name_prefix + '.Channel.Type.RoomList'
    assert props[tp_name_prefix + '.Channel.TargetHandleType'] == 0
    assert props[tp_name_prefix + '.Channel.TargetHandle'] == 0
    assert props[tp_name_prefix + '.Channel.TargetID'] == ''
    assert props[tp_name_prefix + '.Channel.Requested'] == True
    assert props[tp_name_prefix + '.Channel.InitiatorHandle'] \
            == conn.GetSelfHandle()
    assert props[tp_name_prefix + '.Channel.InitiatorID'] \
            == self_name
    assert props[tp_name_prefix + '.Channel.Type.RoomList.Server'] == ''

    assert new_sig.args[0][0][0] == path2
    assert new_sig.args[0][0][1] == props

    assert old_sig.args[0] == path2
    assert old_sig.args[1] == tp_name_prefix + '.Channel.Type.RoomList'
    assert old_sig.args[2] == 0     # handle type
    assert old_sig.args[3] == 0     # handle
    assert old_sig.args[4] == 1     # suppress handler

    assert chan.Get(
            tp_name_prefix + '.Channel.Type.RoomList', 'Server',
            dbus_interface='org.freedesktop.DBus.Properties') == ''

    # FIXME: actually list the rooms!


    # ensure roomlist channel
    call_async(q, requestotron, 'EnsureChannel',
            { tp_name_prefix + '.Channel.ChannelType':
                tp_name_prefix + '.Channel.Type.RoomList',
              tp_name_prefix + '.Channel.TargetHandleType': 0,
              })

    ret = q.expect('dbus-return', method='EnsureChannel')
    yours, ensured_path, ensured_props = ret.value

    assert not yours
    assert ensured_path == path2, (ensured_path, path2)

    conn.Disconnect()

    q.expect_many(
            EventPattern('dbus-signal', signal='Closed',
                path=path1),
            EventPattern('dbus-signal', signal='Closed',
                path=path2),
            EventPattern('dbus-signal', signal='ChannelClosed', args=[path1]),
            EventPattern('dbus-signal', signal='ChannelClosed', args=[path2]),
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]),
            )

if __name__ == '__main__':
    exec_test(test)

