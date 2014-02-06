"""
Test receiving and sending muc invitations
"""

import avahi
import dbus

from saluttest import exec_test
from saluttest import (exec_test, wait_for_contact_in_publish)
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import (make_channel_proxy, assertEquals, assertContains)

from twisted.words.xish import domish

import constants as cs

HT_CONTACT = 1
HT_ROOM = 2

NS_CLIQUE = "http://telepathy.freedesktop.org/xmpp/clique"

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])
    basic_txt = { "txtvers": "1", "status": "avail" }

    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")
    self_handle_name =  conn.Properties.Get(cs.CONN, "SelfID")

    contact_name = "test-muc@" + get_host_name()
    listener, port = setup_stream_listener(q, contact_name)

    AvahiAnnouncer(contact_name, "_presence._tcp", port, basic_txt)

    handle = wait_for_contact_in_publish(q, bus, conn, contact_name)

    requests_iface = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)

    # Create a connection to send the muc invite
    AvahiListener(q).listen_for_service("_presence._tcp")
    e = q.expect('service-added', name = self_handle_name,
        protocol = avahi.PROTO_INET)
    service = e.service
    service.resolve()

    e = q.expect('service-resolved', service = service)

    outbound = connect_to_stream(q, contact_name,
        self_handle_name, str(e.pt), e.port)

    e = q.expect('connection-result')
    assert e.succeeded, e.reason
    e = q.expect('stream-opened', connection = outbound)

    # connected to Salut, now send the muc invite
    msg = domish.Element((None, 'message'))
    msg['to'] = self_handle_name
    msg['from'] = contact_name
    msg['type'] = 'normal'
    body = msg.addElement('body', content='You got a Clique chatroom invitation')
    invite = msg.addElement((NS_CLIQUE, 'invite'))
    invite.addElement('roomname', content='my-room')
    invite.addElement('reason', content='Inviting to this room')
    # FIXME: we should create a real Clique room and use its IP and port.
    # Hardcode values for now. The IP is a multicast address.
    invite.addElement('address', content='239.255.71.249')
    invite.addElement('port', content='62472')
    outbound.send(msg)

    e = q.expect('dbus-signal', signal='NewChannels',
            predicate=lambda e:
                e.args[0][0][1][cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT)
    channels = e.args[0]
    assert len(channels) == 1
    path, props = channels[0]
    channel = make_channel_proxy(conn, path, "Channel")
    channel_group = make_channel_proxy(conn, path, "Channel.Interface.Group")

    # check channel properties
    # org.freedesktop.Telepathy.Channel D-Bus properties
    assert props[cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT
    assertContains(cs.CHANNEL_IFACE_GROUP, props[cs.INTERFACES])
    assertContains(cs.CHANNEL_IFACE_MESSAGES, props[cs.INTERFACES])
    assert props[cs.TARGET_ID] == 'my-room'
    assert props[cs.TARGET_HANDLE_TYPE] == HT_ROOM
    assert props[cs.REQUESTED] == False
    assert props[cs.INITIATOR_HANDLE] == handle
    assert props[cs.INITIATOR_ID] == contact_name

    # we are added to local pending
    e = q.expect('dbus-signal', signal='MembersChanged')
    msg, added, removed, lp, rp, actor, reason = e.args
    assert msg == 'Inviting to this room'
    assert added == []
    assert removed == []
    assert lp == [self_handle]
    assert rp == []
    assert actor == handle
    assert reason == 4       # invited

    # TODO: join the muc, check if we are added to remote-pending and then
    # to members. This would need some tweak in Salut and/or the test framework
    # as Clique takes too much time to join the room and so the event times
    # out.

    # TODO: test sending invitations

    # FIXME: fd.o #30531: this ought to work, but doesn't
    #channel_group.RemoveMembersWithReason([self_handle], "bored now", 0)
    channel.Close()
    q.expect('dbus-signal', signal='Closed')

if __name__ == '__main__':
    exec_test(test)
