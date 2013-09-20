
"""
Test requesting of muc tubes channels using the old and new request API.
"""

import dbus
import avahitest

from twisted.words.xish import domish

from saluttest import exec_test
from servicetest import call_async, EventPattern, wrap_channel, pretty
import constants as cs

def test(q, bus, conn):
    self_name = 'testsuite' + '@' + avahitest.get_host_name()

    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    # check if we can request tube channels
    properties = conn.Properties.GetAll(cs.CONN_IFACE_REQUESTS)
    assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
             cs.TARGET_HANDLE_TYPE: cs.HT_ROOM},
             [cs.TARGET_HANDLE, cs.TARGET_ID, cs.STREAM_TUBE_SERVICE],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # create muc channel using new API
    call_async(q, conn.Requests, 'CreateChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
              cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
              cs.TARGET_ID: 'my-second-room',
              cs.STREAM_TUBE_SERVICE: 'loldongs',
              })

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    tube_path = ret.value[0]
    chan = wrap_channel(bus.get_object(conn.bus_name, tube_path),
                        'StreamTube')

    tube_props = ret.value[1]
    assert tube_props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAM_TUBE
    assert tube_props[cs.TARGET_HANDLE_TYPE] == cs.HT_ROOM
    assert tube_props[cs.TARGET_ID] == 'my-second-room'
    assert tube_props[cs.REQUESTED] == True
    assert tube_props[cs.INITIATOR_HANDLE] == conn.Properties.Get(cs.CONN, "SelfHandle")
    assert tube_props[cs.INITIATOR_ID] == self_name

    # text and tube channels are announced
    channels = new_sig.args[0]
    assert len(channels) == 1

    handle = tube_props[cs.TARGET_HANDLE]

    path, props = channels[0]
    assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAM_TUBE
    assert path == tube_path
    assert props == tube_props
    assert props[cs.TARGET_HANDLE_TYPE] == cs.HT_ROOM
    assert props[cs.TARGET_HANDLE] == handle
    assert props[cs.TARGET_ID] == 'my-second-room'
    assert props[cs.INITIATOR_HANDLE] == conn.Properties.Get(cs.CONN, "SelfHandle")
    assert props[cs.INITIATOR_ID] == self_name

    # ensure the same channel

# TODO: the muc channel doesn't bother to look at existing tubes
# before creating a new one. once that's fixed, uncomment this.
#    yours, ensured_path, _ = conn.Requests.EnsureChannel(
#            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE,
#              cs.TARGET_HANDLE_TYPE: cs.HT_ROOM,
#              cs.TARGET_HANDLE: handle,
#              cs.STREAM_TUBE_SERVICE: 'loldongs',
#              })

#    assert not yours
#    assert ensured_path == tube_path, (ensured_path, tube_path)

    conn.Disconnect()

    q.expect_many(
            EventPattern('dbus-signal', signal='Closed',
                path=tube_path),
            EventPattern('dbus-signal', signal='ChannelClosed', args=[tube_path]),
            EventPattern('dbus-signal', signal='StatusChanged', args=[2, 1]),
            )

if __name__ == '__main__':
    exec_test(test)

