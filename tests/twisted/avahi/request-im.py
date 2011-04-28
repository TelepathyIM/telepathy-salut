
"""
Test requesting of text 1-1 channels using the old and new request API.
"""

import dbus

from saluttest import (exec_test, wait_for_contact_list,
        wait_for_contact_in_publish)
from servicetest import call_async, EventPattern, \
        tp_name_prefix, make_channel_proxy
from avahitest import get_host_name, AvahiAnnouncer
from xmppstream import setup_stream_listener

CHANNEL_TYPE_TEXT = 'org.freedesktop.Telepathy.Channel.Type.Text'

HT_CONTACT = 1

def test(q, bus, conn):
    self_name = 'testsuite' + '@' + get_host_name()

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])

    basic_txt = { "txtvers": "1", "status": "avail" }
    contact_name = "test-request-im@" + get_host_name()
    listener, port = setup_stream_listener(q, contact_name)

    # FIXME: this is a hack to be sure to have all the contact list channels
    # announced so they won't interfere with the muc ones announces.
    wait_for_contact_list(q, conn)

    AvahiAnnouncer(contact_name, "_presence._tcp", port, basic_txt)

    handle = wait_for_contact_in_publish(q, bus, conn, contact_name)

    # check if we can request roomlist channels
    properties = conn.GetAll(
            tp_name_prefix + '.Connection.Interface.Requests',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert ({tp_name_prefix + '.Channel.ChannelType':
                CHANNEL_TYPE_TEXT,
             tp_name_prefix + '.Channel.TargetHandleType': HT_CONTACT,
             },
             [tp_name_prefix + '.Channel.TargetHandle',
              tp_name_prefix + '.Channel.TargetID'],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    # request a muc channel using the old API
    handle = conn.RequestHandles(HT_CONTACT, [contact_name])[0]
    call_async(q, conn, 'RequestChannel', CHANNEL_TYPE_TEXT, HT_CONTACT, handle, True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    path1 = ret.value[0]
    chan = make_channel_proxy(conn, path1, "Channel")

    assert new_sig.args[0][0][0] == path1

    props = new_sig.args[0][0][1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            CHANNEL_TYPE_TEXT
    assert props[tp_name_prefix + '.Channel.TargetHandleType'] == HT_CONTACT
    assert props[tp_name_prefix + '.Channel.TargetHandle'] == handle
    assert props[tp_name_prefix + '.Channel.TargetID'] == contact_name
    assert props[tp_name_prefix + '.Channel.Requested'] == True
    assert props[tp_name_prefix + '.Channel.InitiatorHandle'] \
            == conn.GetSelfHandle()
    assert props[tp_name_prefix + '.Channel.InitiatorID'] \
            == self_name

    assert old_sig.args[0] == path1
    assert old_sig.args[1] == CHANNEL_TYPE_TEXT
    assert old_sig.args[2] == HT_CONTACT     # handle type
    assert old_sig.args[3] == handle         # handle

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = chan.GetAll(
            tp_name_prefix + '.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert channel_props.get('TargetHandle') == handle,\
            channel_props.get('TargetHandle')
    assert channel_props['TargetID'] == contact_name, channel_props
    assert channel_props.get('TargetHandleType') == HT_CONTACT,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == \
            CHANNEL_TYPE_TEXT, channel_props.get('ChannelType')
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorID'] == self_name
    assert channel_props['InitiatorHandle'] == conn.GetSelfHandle()

    requestotron = dbus.Interface(conn,
            tp_name_prefix + '.Connection.Interface.Requests')

    chan.Close()
    q.expect_many(
        EventPattern('dbus-signal', signal='ChannelClosed', args=[path1]),
        EventPattern('dbus-signal', signal='Closed', path=path1))

    # create muc channel using new API
    call_async(q, requestotron, 'CreateChannel',
            { tp_name_prefix + '.Channel.ChannelType':
                CHANNEL_TYPE_TEXT,
              tp_name_prefix + '.Channel.TargetHandleType': HT_CONTACT,
              tp_name_prefix + '.Channel.TargetID': contact_name,
              })

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )
    path2 = ret.value[0]
    chan = make_channel_proxy(conn, path2, "Channel")

    props = ret.value[1]
    assert props[tp_name_prefix + '.Channel.ChannelType'] ==\
            CHANNEL_TYPE_TEXT
    assert props[tp_name_prefix + '.Channel.TargetHandleType'] == HT_CONTACT
    assert props[tp_name_prefix + '.Channel.TargetHandle'] == handle
    assert props[tp_name_prefix + '.Channel.TargetID'] == contact_name
    assert props[tp_name_prefix + '.Channel.Requested'] == True
    assert props[tp_name_prefix + '.Channel.InitiatorHandle'] \
            == conn.GetSelfHandle()
    assert props[tp_name_prefix + '.Channel.InitiatorID'] \
            == self_name

    assert new_sig.args[0][0][0] == path2
    assert new_sig.args[0][0][1] == props

    assert old_sig.args[0] == path2
    assert old_sig.args[1] == CHANNEL_TYPE_TEXT
    assert old_sig.args[2] == HT_CONTACT     # handle type
    assert old_sig.args[3] == handle      # handle
    assert old_sig.args[4] == True        # suppress handler

    # ensure roomlist channel
    yours, ensured_path, ensured_props = requestotron.EnsureChannel(
            { tp_name_prefix + '.Channel.ChannelType':
                CHANNEL_TYPE_TEXT,
              tp_name_prefix + '.Channel.TargetHandleType': HT_CONTACT,
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
