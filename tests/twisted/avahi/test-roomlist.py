
"""
Test MUC support.
"""

import dbus
import avahitest

from twisted.words.xish import domish

from saluttest import exec_test
from servicetest import call_async, lazy, match, EventPattern, \
        tp_name_prefix, tp_path_prefix, make_channel_proxy

CHANNEL_TYPE_ROOMLIST = 'org.freedesktop.Telepathy.Channel.Type.RoomList'

def test(q, bus, conn):
    self_name = 'testsuite' + '@' + avahitest.get_host_name()

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    # check if we can request roomlist channels
    properties = conn.GetAll(
            tp_name_prefix + '.Connection.Interface.Requests',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert ({tp_name_prefix + '.Channel.ChannelType':
                CHANNEL_TYPE_ROOMLIST,
             tp_name_prefix + '.Channel.TargetHandleType': 0,
             },
             [],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # request a roomlist channel using the old API
    call_async(q, conn, 'RequestChannel', CHANNEL_TYPE_ROOMLIST, 0, 0, True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    path1 = ret.value[0]
    chan = make_channel_proxy(conn, path1, "Channel.Type.RoomList")

    assert new_sig.args[0][0][0] == path1

    props = new_sig.args[0][0][1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            CHANNEL_TYPE_ROOMLIST
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
    assert old_sig.args[1] == CHANNEL_TYPE_ROOMLIST
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
            CHANNEL_TYPE_ROOMLIST, channel_props.get('ChannelType')
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == self_name
    assert channel_props['InitiatorHandle'] == conn.GetSelfHandle()

    assert chan.Get(
            CHANNEL_TYPE_ROOMLIST, 'Server',
            dbus_interface='org.freedesktop.DBus.Properties') == ''

    # list rooms
    chan.ListRooms()

    q.expect('dbus-signal', signal='ListingRooms', args=[True])

    e = q.expect('dbus-signal', signal='GotRooms')
    rooms = e.args[0]
    assert rooms == []

    q.expect('dbus-signal', signal='ListingRooms', args=[False])

    # FIXME: announce some Clique rooms and check is they are properly listed

    requestotron = dbus.Interface(conn,
            tp_name_prefix + '.Connection.Interface.Requests')

    # create roomlist channel using new API
    call_async(q, requestotron, 'CreateChannel',
            { tp_name_prefix + '.Channel.ChannelType':
                CHANNEL_TYPE_ROOMLIST,
              tp_name_prefix + '.Channel.TargetHandleType': 0,
              })

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    path2 = ret.value[0]
    chan = make_channel_proxy(conn, path2, "Channel.Type.RoomList")

    props = ret.value[1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            CHANNEL_TYPE_ROOMLIST
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
    assert old_sig.args[1] == CHANNEL_TYPE_ROOMLIST
    assert old_sig.args[2] == 0     # handle type
    assert old_sig.args[3] == 0     # handle
    assert old_sig.args[4] == 1     # suppress handler

    assert chan.Get(
            CHANNEL_TYPE_ROOMLIST, 'Server',
            dbus_interface='org.freedesktop.DBus.Properties') == ''

    # FIXME: actually list the rooms!


    # ensure roomlist channel
    call_async(q, requestotron, 'EnsureChannel',
            { tp_name_prefix + '.Channel.ChannelType':
                CHANNEL_TYPE_ROOMLIST,
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

