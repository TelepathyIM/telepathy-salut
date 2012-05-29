
"""
Test requesting of muc tubes channels using the old and new request API.
"""

import dbus
import avahitest

from twisted.words.xish import domish

from saluttest import exec_test, wait_for_contact_list
from servicetest import call_async, EventPattern, wrap_channel
from constants import *

def test(q, bus, conn):
    self_name = 'testsuite' + '@' + avahitest.get_host_name()

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    # FIXME: this is a hack to be sure to have all the contact list channels
    # announced so they won't interfere with the muc ones announces.
    wait_for_contact_list(q, conn)

    # check if we can request roomlist channels
    properties = conn.Properties.GetAll(CONN_IFACE_REQUESTS)
    assert ({CHANNEL_TYPE: CHANNEL_TYPE_TUBES,
             TARGET_HANDLE_TYPE: HT_ROOM},
             [TARGET_HANDLE, TARGET_ID],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # create muc channel using new API
    call_async(q, conn.Requests, 'CreateChannel',
            { CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
              TARGET_HANDLE_TYPE: HT_ROOM,
              TARGET_ID: 'my-second-room',
              STREAM_TUBE_SERVICE: 'loldongs',
              })

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    path2 = ret.value[0]
    chan = wrap_channel(bus.get_object(conn.bus_name, path2),
                        'StreamTube')

    handle = conn.RequestHandles(HT_ROOM, ['my-second-room'])[0]

    tube_props = ret.value[1]
    assert tube_props[CHANNEL_TYPE] == CHANNEL_TYPE_STREAM_TUBE
    assert tube_props[TARGET_HANDLE_TYPE] == HT_ROOM
    assert tube_props[TARGET_HANDLE] == handle
    assert tube_props[TARGET_ID] == 'my-second-room'
    assert tube_props[REQUESTED] == True
    assert tube_props[INITIATOR_HANDLE] == conn.GetSelfHandle()
    assert tube_props[INITIATOR_ID] == self_name

    # text and tube channels are announced
    channels = new_sig.args[0]
    assert len(channels) == 3
    got_text, got_tubes, got_tube = False, False, False

    for path, props in channels:
        if props[CHANNEL_TYPE] == CHANNEL_TYPE_TEXT:
            got_text = True
            assert props[REQUESTED] == False
        elif props[CHANNEL_TYPE] == CHANNEL_TYPE_TUBES:
            got_tubes = True
            assert props[REQUESTED] == False
        elif props[CHANNEL_TYPE] == CHANNEL_TYPE_STREAM_TUBE:
            got_tube = True
            assert path == path2
            assert props == tube_props
        else:
            assert False

        assert props[TARGET_HANDLE_TYPE] == HT_ROOM
        assert props[TARGET_HANDLE] == handle
        assert props[TARGET_ID] == 'my-second-room'
        assert props[INITIATOR_HANDLE] == conn.GetSelfHandle()
        assert props[INITIATOR_ID] == self_name

    # ensure the same channel
    yours, ensured_path, ensured_props = conn.Requests.EnsureChannel(
            { CHANNEL_TYPE: CHANNEL_TYPE_STREAM_TUBE,
              TARGET_HANDLE_TYPE: HT_ROOM,
              TARGET_HANDLE: handle,
              STREAM_TUBE_SERVICE: 'loldongs',
              })

    # TODO: get rid of Tubes and this'll start making more sense
    return

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

