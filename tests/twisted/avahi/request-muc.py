
"""
Test requesting of muc text channels using the old and new request API.
"""

import dbus
import avahitest

from twisted.words.xish import domish

from saluttest import exec_test, wait_for_contact_list
from servicetest import call_async, EventPattern, \
        tp_name_prefix, tp_path_prefix, make_channel_proxy

import constants as cs

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
                cs.CHANNEL_TYPE_TEXT,
             tp_name_prefix + '.Channel.TargetHandleType': cs.HT_ROOM,
             },
             [tp_name_prefix + '.Channel.TargetHandle',
              tp_name_prefix + '.Channel.TargetID'],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    requestotron = dbus.Interface(conn,
            tp_name_prefix + '.Connection.Interface.Requests')

    # create muc channel using new API
    call_async(q, requestotron, 'CreateChannel',
            { tp_name_prefix + '.Channel.ChannelType':
                cs.CHANNEL_TYPE_TEXT,
              tp_name_prefix + '.Channel.TargetHandleType': cs.HT_ROOM,
              tp_name_prefix + '.Channel.TargetID': 'my-second-room',
              })

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    path2 = ret.value[0]
    chan = make_channel_proxy(conn, path2, "Channel")

    props = ret.value[1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            cs.CHANNEL_TYPE_TEXT
    assert props[tp_name_prefix + '.Channel.TargetHandleType'] == cs.HT_ROOM
    assert props[tp_name_prefix + '.Channel.TargetID'] == 'my-second-room'
    assert props[tp_name_prefix + '.Channel.Requested'] == True
    assert props[tp_name_prefix + '.Channel.InitiatorHandle'] \
            == conn.Properties.Get(cs.CONN, "SelfHandle")
    assert props[tp_name_prefix + '.Channel.InitiatorID'] \
            == self_name

    assert new_sig.args[0][0][0] == path2
    assert new_sig.args[0][0][1] == props

    # ensure roomlist channel
    handle = props[tp_name_prefix + '.Channel.TargetHandle']
    yours, ensured_path, ensured_props = ret.value = requestotron.EnsureChannel(
            { tp_name_prefix + '.Channel.ChannelType':
                cs.CHANNEL_TYPE_TEXT,
              tp_name_prefix + '.Channel.TargetHandleType': cs.HT_ROOM,
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

