from saluttest import exec_test, wait_for_contact_in_publish
from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name, skip_if_another_llxmpp
import avahi

from xmppstream import setup_stream_listener, connect_to_stream, OutgoingXmppiChatStream
from servicetest import make_channel_proxy

from twisted.words.xish import xpath, domish


import time
import dbus

import constants as cs

INCOMING_MESSAGE = "Test 123"

def test(q, bus, conn):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0L, 0L])
    basic_txt = { "txtvers": "1", "status": "avail" }

    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")
    self_handle_name =  conn.Properties.Get(cs.CONN, "SelfID")

    contact_name = "test-ichat-incoming-msg@" + get_host_name()
    listener, port = setup_stream_listener(q, contact_name)

    announcer = AvahiAnnouncer(contact_name, "_presence._tcp", port, basic_txt)

    handle = wait_for_contact_in_publish(q, bus, conn, contact_name)

    # Create a connection to send msg stanza
    AvahiListener(q).listen_for_service("_presence._tcp")
    e = q.expect('service-added', name = self_handle_name,
        protocol = avahi.PROTO_INET)
    service = e.service
    service.resolve()

    e = q.expect('service-resolved', service = service)

    outbound = connect_to_stream(q, contact_name,
        self_handle_name, str(e.pt), e.port, OutgoingXmppiChatStream)

    e = q.expect('connection-result')
    assert e.succeeded, e.reason
    e = q.expect('stream-opened', connection = outbound)

    msg = domish.Element((None, 'message'))
    msg['to'] = self_handle_name
    msg['type'] = 'chat'
    boddy = msg.addElement('body', content='hi')
    outbound.send(msg)

    e = q.expect('dbus-signal', signal='MessageReceived')
    assert len(e.args[0]) == 2
    assert e.args[0][0]['message-sender-id'] == contact_name
    assert e.args[0][0]['message-sender'] == handle
    assert e.args[0][0]['message-type'] == cs.MT_NORMAL
    assert e.args[0][1]['content'] == "hi"

if __name__ == '__main__':
    skip_if_another_llxmpp()
    exec_test(test)
