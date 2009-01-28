
"""
Test requesting of muc tubes channels using the old and new request API.
"""

import dbus
import avahitest

from twisted.words.xish import domish

from saluttest import exec_test, wait_for_contact_list
from servicetest import call_async, lazy, match, EventPattern, \
        tp_name_prefix, tp_path_prefix, make_channel_proxy
from constants import *

CHANNEL_TYPE_TEXT = 'org.freedesktop.Telepathy.Channel.Type.Text'
CHANNEL_TYPE_TUBES = 'org.freedesktop.Telepathy.Channel.Type.Tubes'

HT_ROOM = 2

def test(q, bus, conn):
    self_name = 'testsuite' + '@' + avahitest.get_host_name()

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    # FIXME: this is a hack to be sure to have all the contact list channels
    # announced so they won't interfere with the muc ones announces.
    wait_for_contact_list(q, conn)

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

    # text and tubes channels are announced
    channels = new_sig.args[0]
    assert len(channels) == 2
    got_text, got_tubes = False, False

    for path, props in channels:
        if props[CHANNEL_TYPE] == CHANNEL_TYPE_TEXT:
            got_text = True
            assert props[tp_name_prefix + '.Channel.Requested'] == False
        elif props[CHANNEL_TYPE] == CHANNEL_TYPE_TUBES:
            got_tubes = True
            assert props[tp_name_prefix + '.Channel.Requested'] == True
        else:
            assert False

        assert props[tp_name_prefix + '.Channel.TargetHandleType'] == HT_ROOM
        assert props[tp_name_prefix + '.Channel.TargetHandle'] == handle
        assert props[tp_name_prefix + '.Channel.TargetID'] == 'my-first-room'
        assert props[tp_name_prefix + '.Channel.InitiatorHandle'] \
                == conn.GetSelfHandle()
        assert props[tp_name_prefix + '.Channel.InitiatorID'] \
                == self_name

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

    tubes_props = ret.value[1]
    assert tubes_props[tp_name_prefix + '.Channel.ChannelType'] ==\
            CHANNEL_TYPE_TUBES
    assert tubes_props[tp_name_prefix + '.Channel.TargetHandleType'] == HT_ROOM
    assert tubes_props[tp_name_prefix + '.Channel.TargetHandle'] == handle
    assert tubes_props[tp_name_prefix + '.Channel.TargetID'] == 'my-second-room'
    assert tubes_props[tp_name_prefix + '.Channel.Requested'] == True
    assert tubes_props[tp_name_prefix + '.Channel.InitiatorHandle'] \
            == conn.GetSelfHandle()
    assert tubes_props[tp_name_prefix + '.Channel.InitiatorID'] \
            == self_name

    # text and tubes channels are announced
    channels = new_sig.args[0]
    assert len(channels) == 2
    got_text, got_tubes = False, False

    for path, props in channels:
        if props[CHANNEL_TYPE] == CHANNEL_TYPE_TEXT:
            got_text = True
            assert props[tp_name_prefix + '.Channel.Requested'] == False
        elif props[CHANNEL_TYPE] == CHANNEL_TYPE_TUBES:
            got_tubes = True
            assert props == tubes_props
            assert path == path2
        else:
            assert False

        assert props[tp_name_prefix + '.Channel.TargetHandleType'] == HT_ROOM
        assert props[tp_name_prefix + '.Channel.TargetHandle'] == handle
        assert props[tp_name_prefix + '.Channel.TargetID'] == 'my-second-room'
        assert props[tp_name_prefix + '.Channel.InitiatorHandle'] \
                == conn.GetSelfHandle()
        assert props[tp_name_prefix + '.Channel.InitiatorID'] \
                == self_name

    # ensure roomlist channel
    yours, ensured_path, ensured_props = requestotron.EnsureChannel(
            { tp_name_prefix + '.Channel.ChannelType':
                CHANNEL_TYPE_TUBES,
              tp_name_prefix + '.Channel.TargetHandleType': HT_ROOM,
              tp_name_prefix + '.Channel.TargetHandle': handle,
              })

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

