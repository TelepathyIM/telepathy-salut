
"""
Test room list support.
"""

import dbus
import avahitest

from twisted.words.xish import domish

from saluttest import exec_test, wait_for_contact_list
from servicetest import call_async, EventPattern, \
        tp_name_prefix, tp_path_prefix, wrap_channel
import constants as cs

def test(q, bus, conn):
    self_name = 'testsuite' + '@' + avahitest.get_host_name()

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    # FIXME: this is a hack to be sure to have all the contact list channels
    # announced so they won't interfere with the roomlist ones announces.
    wait_for_contact_list(q, conn)

    # check if we can request roomlist channels
    properties = conn.GetAll(
            tp_name_prefix + '.Connection.Interface.Requests',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert ({tp_name_prefix + '.Channel.ChannelType':
                cs.CHANNEL_TYPE_ROOM_LIST,
             tp_name_prefix + '.Channel.TargetHandleType': 0,
             },
             [],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # request a roomlist channel using the old API
    call_async(q, conn, 'RequestChannel', cs.CHANNEL_TYPE_ROOMLIST, 0, 0, True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    path1 = ret.value[0]
    chan = wrap_channel(bus.get_object(conn.bus_name, path1), "RoomList")

    assert new_sig.args[0][0][0] == path1

    props = new_sig.args[0][0][1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            cs.CHANNEL_TYPE_ROOMLIST
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
    assert old_sig.args[1] == cs.CHANNEL_TYPE_ROOMLIST
    assert old_sig.args[2] == 0     # handle type
    assert old_sig.args[3] == 0     # handle
    assert old_sig.args[4] == 1     # suppress handler

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = chan.Properties.GetAll(
            tp_name_prefix + '.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert channel_props.get('TargetHandle') == 0,\
            channel_props.get('TargetHandle')
    assert channel_props['TargetID'] == '', channel_props
    assert channel_props.get('TargetHandleType') == 0,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == \
            cs.CHANNEL_TYPE_ROOMLIST, channel_props.get('ChannelType')
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == self_name
    assert channel_props['InitiatorHandle'] == conn.GetSelfHandle()

    assert chan.Get(
            cs.CHANNEL_TYPE_ROOMLIST, 'Server',
            dbus_interface='org.freedesktop.DBus.Properties') == ''

    # list rooms
    chan.RoomList.ListRooms()

    q.expect('dbus-signal', signal='ListingRooms', args=[True])

    e = q.expect('dbus-signal', signal='GotRooms')
    rooms = e.args[0]
    # FIXME: this will fail if there is room announced on the network
    #assert rooms == []

    q.expect('dbus-signal', signal='ListingRooms', args=[False])

    # FIXME: announce some Clique rooms and check is they are properly listed

    requestotron = dbus.Interface(conn,
            tp_name_prefix + '.Connection.Interface.Requests')

    # create roomlist channel using new API
    call_async(q, requestotron, 'CreateChannel',
            { tp_name_prefix + '.Channel.ChannelType':
                cs.CHANNEL_TYPE_ROOM_LIST,
              tp_name_prefix + '.Channel.TargetHandleType': 0,
              })

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    path2 = ret.value[0]
    chan2 = wrap_channel(bus.get_object(conn.bus_name, path2), "RoomList")

    props = ret.value[1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            cs.CHANNEL_TYPE_ROOM_LIST
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
    assert old_sig.args[1] == cs.CHANNEL_TYPE_ROOM_LIST
    assert old_sig.args[2] == 0     # handle type
    assert old_sig.args[3] == 0     # handle
    assert old_sig.args[4] == 1     # suppress handler

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
            EventPattern('dbus-signal', signal='Closed',
                path=path1),
            EventPattern('dbus-signal', signal='ChannelClosed', args=[path1]),
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]),
            )

if __name__ == '__main__':
    exec_test(test)

