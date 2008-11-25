
"""
Test MUC tubes support.
"""

import dbus
import avahitest

from twisted.words.xish import domish

from saluttest import exec_test
from servicetest import call_async, lazy, match, EventPattern, \
        tp_name_prefix, tp_path_prefix, make_channel_proxy

CHANNEL_TYPE_TUBES = 'org.freedesktop.Telepathy.Channel.Type.Tubes'

HT_ROOM = 2

def test(q, bus, conn):
    self_name = 'testsuite' + '@' + avahitest.get_host_name()

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    # FIXME: this is a hack to be sure to have all the contact list channels
    # announced so they won't interfere with the muc ones announces.
    # publish
    q.expect('dbus-signal', signal='NewChannel')
    # subscribe
    q.expect('dbus-signal', signal='NewChannel')
    # known
    q.expect('dbus-signal', signal='NewChannel')

    # check if we can request roomlist channels
    properties = conn.GetAll(
            tp_name_prefix + '.Connection.Interface.Requests',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert ({tp_name_prefix + '.Channel.ChannelType':
                CHANNEL_TYPE_TUBES,
             tp_name_prefix + '.Channel.TargetHandleType': HT_ROOM,
             },
             [tp_name_prefix + '.Channel.TargetHandle',
              tp_name_prefix + '.Channel.TargetID'],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # request a muc tubes channel using the old API
    handle = conn.RequestHandles(HT_ROOM, ['my-first-room'])[0]
    call_async(q, conn, 'RequestChannel', CHANNEL_TYPE_TUBES, HT_ROOM, handle, True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    path1 = ret.value[0]
    chan = make_channel_proxy(conn, path1, "Channel")

    # first the text channel is announced
    # FIXME: check text channel

    # then the tubes channel is announced
    old_sig, new_sig = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert new_sig.args[0][0][0] == path1

    props = new_sig.args[0][0][1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            CHANNEL_TYPE_TUBES
    assert props[tp_name_prefix + '.Channel.TargetHandleType'] == HT_ROOM
    assert props[tp_name_prefix + '.Channel.TargetHandle'] == handle
    assert props[tp_name_prefix + '.Channel.TargetID'] == 'my-first-room'
    assert props[tp_name_prefix + '.Channel.Requested'] == True
    assert props[tp_name_prefix + '.Channel.InitiatorHandle'] \
            == conn.GetSelfHandle()
    assert props[tp_name_prefix + '.Channel.InitiatorID'] \
            == self_name

    assert old_sig.args[0] == path1
    assert old_sig.args[1] == CHANNEL_TYPE_TUBES
    assert old_sig.args[2] == HT_ROOM     # handle type
    assert old_sig.args[3] == handle     # handle

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = chan.GetAll(
            tp_name_prefix + '.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert channel_props.get('TargetHandle') == handle,\
            channel_props.get('TargetHandle')
    assert channel_props['TargetID'] == 'my-first-room', channel_props
    assert channel_props.get('TargetHandleType') == HT_ROOM,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == \
            CHANNEL_TYPE_TUBES, channel_props.get('ChannelType')
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == self_name
    assert channel_props['InitiatorHandle'] == conn.GetSelfHandle()

    requestotron = dbus.Interface(conn,
            tp_name_prefix + '.Connection.Interface.Requests')

    # create muc channel using new API
    call_async(q, requestotron, 'CreateChannel',
            { tp_name_prefix + '.Channel.ChannelType':
                CHANNEL_TYPE_TUBES,
              tp_name_prefix + '.Channel.TargetHandleType': HT_ROOM,
              tp_name_prefix + '.Channel.TargetID': 'my-second-room',
              })

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    path2 = ret.value[0]
    chan = make_channel_proxy(conn, path2, "Channel")

    handle = conn.RequestHandles(HT_ROOM, ['my-second-room'])[0]

    props = ret.value[1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            CHANNEL_TYPE_TUBES
    assert props[tp_name_prefix + '.Channel.TargetHandleType'] == HT_ROOM
    assert props[tp_name_prefix + '.Channel.TargetHandle'] == handle
    assert props[tp_name_prefix + '.Channel.TargetID'] == 'my-second-room'
    assert props[tp_name_prefix + '.Channel.Requested'] == True
    assert props[tp_name_prefix + '.Channel.InitiatorHandle'] \
            == conn.GetSelfHandle()
    assert props[tp_name_prefix + '.Channel.InitiatorID'] \
            == self_name

    # first the text channel is announced
    # FIXME: check text channel

    # then the tubes channel is announced
    old_sig, new_sig = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert new_sig.args[0][0][0] == path2
    assert new_sig.args[0][0][1] == props

    assert old_sig.args[0] == path2
    assert old_sig.args[1] == CHANNEL_TYPE_TUBES
    assert old_sig.args[2] == HT_ROOM     # handle type
    assert old_sig.args[3] == handle      # handle
    assert old_sig.args[4] == True        # suppress handler

    # ensure roomlist channel
    call_async(q, requestotron, 'EnsureChannel',
            { tp_name_prefix + '.Channel.ChannelType':
                CHANNEL_TYPE_TUBES,
              tp_name_prefix + '.Channel.TargetHandleType': HT_ROOM,
              tp_name_prefix + '.Channel.TargetHandle': handle,
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

