from saluttest import exec_test, wait_for_contact_in_publish
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
import avahi
import constants as cs

from xmppstream import setup_stream_listener, connect_to_stream
from servicetest import make_channel_proxy

from twisted.words.xish import domish

import dbus

NS_CLIQUE = "http://telepathy.freedesktop.org/xmpp/clique"

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])
    basic_txt = { "txtvers": "1", "status": "avail" }

    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")
    self_handle_name =  conn.Properties.Get(cs.CONN, "SelfID")

    contact_name = "test-text-channel@" + get_host_name()
    listener, port = setup_stream_listener(q, contact_name)

    announcer = AvahiAnnouncer(contact_name, "_presence._tcp", port, basic_txt)

    handle = wait_for_contact_in_publish(q, bus, conn, contact_name)

    # create a clique room
    basic_txt = { "txtvers": "0"}
    AvahiAnnouncer("myroom", "_clique._udp", 41377, basic_txt)

    # connect a stream
    AvahiListener(q).listen_for_service("_presence._tcp")
    e = q.expect('service-added', name = self_handle_name,
        protocol = avahi.PROTO_INET)
    service = e.service
    service.resolve()

    e = q.expect('service-resolved', service = service)

    xmpp_connection = connect_to_stream(q, contact_name,
        self_handle_name, str(e.pt), e.port)

    e = q.expect('connection-result')
    assert e.succeeded, e.reason

    e = q.expect('stream-opened', connection = xmpp_connection)

    # send an invitation
    message = domish.Element(('', 'message'))
    message['type'] = 'normal'
    message.addElement('body', content='You got a Clique chatroom invitation')
    invite = message.addElement((NS_CLIQUE, 'invite'))
    invite.addElement('roomname', content='myroom')
    invite.addElement('reason', content='Inviting to this room')
    invite.addElement('address', content='127.0.0.1')
    invite.addElement('port', content='41377')
    xmpp_connection.send(message)

    # group channel is created
    e = q.expect('dbus-signal', signal='NewChannel',
            predicate=lambda e:
                e.args[1] == cs.CHANNEL_TYPE_TEXT and
                e.args[2] == cs.HT_ROOM)
    path = e.args[0]
    channel = make_channel_proxy(conn, path, 'Channel')
    props_iface = dbus.Interface(bus.get_object(conn.object.bus_name, path),
        dbus.PROPERTIES_IFACE)

    q.expect('dbus-signal', signal='MembersChanged', path=path)

    lp_members = props_iface.Get('org.freedesktop.Telepathy.Channel.Interface.Group',
        'LocalPendingMembers')

    assert len(lp_members) == 1
    added, actor, reason, msg = lp_members[0]

    assert added == self_handle
    assert actor == handle
    assert reason == 4                       #invited
    assert msg == 'Inviting to this room'

    # decline invitation
    channel.Close()
    q.expect('dbus-signal', signal='Closed')

if __name__ == '__main__':
    exec_test(test)
