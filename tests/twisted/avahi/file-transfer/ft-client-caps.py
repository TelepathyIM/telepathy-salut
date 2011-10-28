
"""
Test FT capabilities with Connection.Interface.ContactCapabilities

1. Receive presence and caps from contacts and check that
GetContactCapabilities works correctly and that ContactCapabilitiesChanged is
correctly received. Also check that GetContactAttributes gives the same
results.

- no FT cap at all
- FT caps without metadata extension
- FT caps with metadata extension
- 1 FT cap with a service name
- 2 FT caps with service names
- 1 FT cap again, to test whether the caps cache works with FT services

2. Test UpdateCapabilities and test that a presence stanza is sent to the
contacts, test that the D-Bus signal ContactCapabilitiesChanged is fired for
the self handle, ask Salut for its caps with an iq request, check the reply
is correct, and ask Salut for its caps using D-Bus method
GetContactCapabilities. Also check that GetContactAttributes gives the same
results.

Ensure that just a Requested=True channel class in a client filter doesn't
make a FT service advertised as a cap.

- no FT cap at all
- 1 FT cap with no service name
- 1 Requested=True FT cap with service name
- 1 FT cap with service name
- 1 FT cap with service name + 1 FT cap with no service name
- 2 FT caps with service names
- 1 FT cap with service name again, just for fun

"""

import dbus

from avahitest import AvahiAnnouncer, AvahiListener
from avahitest import get_host_name
from avahitest import txt_get_key
import avahi

from xmppstream import connect_to_stream, setup_stream_listener

from twisted.words.xish import xpath

from servicetest import assertEquals, assertLength, assertContains,\
        assertDoesNotContain, sync_dbus, EventPattern
from saluttest import exec_test, make_result_iq, sync_stream, make_presence, \
    fixed_features
import constants as cs

from caps_helper import check_caps, compute_caps_hash, text_fixed_properties, \
    text_allowed_properties, caps_contain, disco_caps, \
    ft_fixed_properties, ft_allowed_properties, ft_allowed_properties_with_metadata
import ns
from config import PACKAGE_STRING

no_service_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
    })
bidir_daap_fixed_properties = dbus.Dictionary(
    no_service_fixed_properties.items() + {
    cs.FT_SERVICE_NAME: 'daap'
    }.items())
outgoing_daap_fixed_properties = dbus.Dictionary(
    bidir_daap_fixed_properties.items() + {
    cs.REQUESTED : True,
    }.items())
incoming_daap_fixed_properties = dbus.Dictionary(
    bidir_daap_fixed_properties.items() + {
    cs.REQUESTED : False,
    }.items())
http_fixed_properties = dbus.Dictionary(
    no_service_fixed_properties.items() + {
    cs.FT_SERVICE_NAME: 'http'
    }.items())
xiangqi_fixed_properties = dbus.Dictionary(
    no_service_fixed_properties.items() + {
    cs.FT_SERVICE_NAME: 'com.example.Xiangqi'
    }.items())
go_fixed_properties = dbus.Dictionary(
    no_service_fixed_properties.items() + {
    cs.FT_SERVICE_NAME: 'com.example.Go'
    }.items())

client = 'http://telepathy.freedesktop.org/fake-client'

def assertSameElements(a, b):
    assertEquals(sorted(a), sorted(b))

def receive_caps(q, bus, conn, service, contact, contact_handle, features,
                 expected_caps, expect_disco=True, expect_ccc=True):

    ver = compute_caps_hash([], features, {})
    txt_record = { "txtvers": "1", "status": "avail",
                   "node": client, "ver": ver, "hash": "sha-1"}

    listener, port = setup_stream_listener(q, contact)
    AvahiAnnouncer(contact, "_presence._tcp", port, txt_record)

    if expect_disco:
        # Salut looks up our capabilities
        e = q.expect('incoming-connection', listener=listener)
        stream = e.connection

        event = q.expect('stream-iq', to=contact, query_ns=ns.DISCO_INFO,
                         connection=stream)
        query_node = xpath.queryForNodes('/iq/query', event.stanza)[0]
        assert query_node.attributes['node'] == \
            client + '#' + ver

        # send good reply
        result = make_result_iq(event.stanza)
        query = result.firstChildElement()
        query['node'] = client + '#' + ver

        for f in features:
            feature = query.addElement('feature')
            feature['var'] = f

        stream.send(result)

    if expect_ccc:
        event = q.expect('dbus-signal', signal='ContactCapabilitiesChanged')
        announced_ccs, = event.args
        assertSameElements(expected_caps, announced_ccs[contact_handle])
    else:
        if expect_disco:
            # Make sure Salut's got the caps
            sync_stream(q, stream)

    caps = conn.ContactCapabilities.GetContactCapabilities([contact_handle])
    assertSameElements(expected_caps, caps[contact_handle])

    # test again, to check GetContactCapabilities does not have side effect
    caps = conn.ContactCapabilities.GetContactCapabilities([contact_handle])
    assertSameElements(expected_caps, caps[contact_handle])

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [contact_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [contact_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertSameElements(caps[contact_handle], caps_via_contacts_iface)

    # close the connection and expect a new one to be opened by Salut
    # the next time we need some discoing doing
    if expect_disco:
        stream.send('</stream:stream>')
        stream.transport.loseConnection()
        # pass some time so Salut knows the connection is lost and
        # won't try and send stuff down a closed connection on the
        # next test.
        sync_dbus(bus, q, conn)

def test_ft_caps_from_contact(q, bus, conn, service, contact):
    contact_handle = conn.RequestHandles(cs.HT_CONTACT, [contact])[0]

    # Check that we don't crash if we haven't seen any caps/presence for this
    # contact yet.
    caps = conn.ContactCapabilities.GetContactCapabilities([contact_handle])

    basic_caps = [(text_fixed_properties, text_allowed_properties)]

    # Since we don't know their caps, they should be omitted from the dict,
    # rather than present with no caps, but all contacts have text chat caps.
    assertEquals([], caps[contact_handle])

    # send presence with no FT cap
    # We don't expect ContactCapabilitiesChanged to be emitted here: we always
    # assume people can do text channels.
    receive_caps(q, bus, conn, service, contact, contact_handle, [], basic_caps,
        expect_ccc=False)

    # send presence with no mention of metadata
    no_metadata_ft_caps = [
        (text_fixed_properties, text_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties)
        ]
    receive_caps(q, bus, conn, service, contact, contact_handle,
        [ns.IQ_OOB], no_metadata_ft_caps)

    # send presence with generic FT caps including metadata from now on
    generic_ft_caps = [
        (text_fixed_properties, text_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties_with_metadata)
        ]
    generic_ft_features = [ns.IQ_OOB, ns.TP_FT_METADATA]
    receive_caps(q, bus, conn, service, contact, contact_handle,
        generic_ft_features, generic_ft_caps)

    # send presence with 1 FT cap with a service
    daap_caps = generic_ft_caps + [
        (bidir_daap_fixed_properties, ft_allowed_properties + [cs.FT_METADATA])]
    receive_caps(q, bus, conn, service, contact, contact_handle,
        generic_ft_features + [ns.TP_FT_METADATA + '#daap'], daap_caps)

    # send presence with 2 FT caps
    daap_xiangqi_caps = daap_caps + [
        (xiangqi_fixed_properties, ft_allowed_properties + [cs.FT_METADATA])]
    receive_caps(q, bus, conn, service, contact, contact_handle,
        generic_ft_features + [ns.TP_FT_METADATA + '#com.example.Xiangqi',
         ns.TP_FT_METADATA + '#daap',
        ], daap_xiangqi_caps)

    # send presence with 1 FT cap again
    # Salut does not look up our capabilities because of the cache
    receive_caps(q, bus, conn, service, contact, contact_handle,
        generic_ft_features + [ns.TP_FT_METADATA + '#daap'], daap_caps,
        expect_disco=False)

def advertise_caps(q, bus, conn, service, filters, expected_features, unexpected_features,
                   expected_caps):
    # make sure nothing from a previous update is still running
    sync_dbus(bus, q, conn)

    self_handle = conn.GetSelfHandle()
    self_handle_name =  conn.InspectHandles(1, [self_handle])[0]
    ret_caps = conn.ContactCapabilities.UpdateCapabilities(
            [(cs.CLIENT + '.Foo', filters, [])])

    presence, event_dbus = q.expect_many(
        EventPattern('service-resolved', service=service),
        EventPattern('dbus-signal', signal='ContactCapabilitiesChanged')
        )
    assertLength(1, event_dbus.args)
    signaled_caps = event_dbus.args[0]

    outbound = connect_to_stream(q, 'test@foobar',
        self_handle_name, str(presence.pt), presence.port)

    e = q.expect('connection-result')
    assert e.succeeded, e.reason

    e = q.expect('stream-opened', connection=outbound)

    # Expect Salut to reply with the correct caps
    event, namespaces = disco_caps(q, outbound, presence.txt)

    assertSameElements(expected_caps, signaled_caps[self_handle])

    assertContains(ns.TP_FT_METADATA, namespaces)

    for var in expected_features:
        assertContains(var, namespaces)

    for var in unexpected_features:
        assertDoesNotContain(var, namespaces)

    # Check our own caps
    caps = conn.ContactCapabilities.GetContactCapabilities([self_handle])
    assertSameElements(expected_caps, caps[self_handle])

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [self_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [self_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertSameElements(caps[self_handle], caps_via_contacts_iface)

    # close things...
    outbound.send('</stream:stream>')
    sync_dbus(bus, q, conn)
    outbound.transport.loseConnection()

def test_ft_caps_to_contact(q, bus, conn, service):
    self_handle = conn.GetSelfHandle()

    basic_caps = [
        (text_fixed_properties, text_allowed_properties),
        (ft_fixed_properties, ft_allowed_properties_with_metadata),
        ]
    daap_caps = basic_caps + [
        (bidir_daap_fixed_properties, ft_allowed_properties + [cs.FT_METADATA]),
        ]
    xiangqi_caps = basic_caps + [
        (xiangqi_fixed_properties, ft_allowed_properties + [cs.FT_METADATA]),
        ]
    xiangqi_go_caps = xiangqi_caps + [
        (go_fixed_properties, ft_allowed_properties + [cs.FT_METADATA]),
        ]
    go_caps = basic_caps + [
        (go_fixed_properties, ft_allowed_properties + [cs.FT_METADATA]),
        ]

    #
    # Check our own caps
    #
    caps = conn.ContactCapabilities.GetContactCapabilities([self_handle])
    assertEquals(basic_caps, caps[self_handle])

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [self_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [self_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertEquals(caps[self_handle], caps_via_contacts_iface)

    #
    # Advertise nothing
    #
    conn.ContactCapabilities.UpdateCapabilities(
            [(cs.CLIENT + '.Foo', {}, [])])

    # Check our own caps
    caps = conn.ContactCapabilities.GetContactCapabilities([self_handle])
    assertLength(1, caps)
    assertEquals(basic_caps, caps[self_handle])

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [self_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [self_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertEquals(caps[self_handle], caps_via_contacts_iface)

    sync_dbus(bus, q, conn)

    #
    # Advertise FT but with no service name
    #
    conn.ContactCapabilities.UpdateCapabilities(
            [(cs.CLIENT + '.Foo', [no_service_fixed_properties], [])])

    # Check our own caps
    caps = conn.ContactCapabilities.GetContactCapabilities([self_handle])
    assertLength(1, caps)
    assertEquals(basic_caps, caps[self_handle])

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [self_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [self_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertEquals(caps[self_handle], caps_via_contacts_iface)

    sync_dbus(bus, q, conn)

    #
    # Advertise a Requested=True FT cap
    #
    conn.ContactCapabilities.UpdateCapabilities(
            [(cs.CLIENT + '.Foo', [outgoing_daap_fixed_properties], [])])

    # Check our own caps
    caps = conn.ContactCapabilities.GetContactCapabilities([self_handle])
    assertLength(1, caps)
    assertEquals(basic_caps, caps[self_handle])

    # check the Contacts interface give the same caps
    caps_via_contacts_iface = conn.Contacts.GetContactAttributes(
            [self_handle], [cs.CONN_IFACE_CONTACT_CAPS], False) \
            [self_handle][cs.ATTR_CONTACT_CAPABILITIES]
    assertEquals(caps[self_handle], caps_via_contacts_iface)

    advertise_caps(q, bus, conn, service,
        [bidir_daap_fixed_properties],
        [ns.TP_FT_METADATA + '#daap'],
        [ns.TP_FT_METADATA + '#http',
         ns.TP_FT_METADATA + '#com.example.Go',
         ns.TP_FT_METADATA + '#com.example.Xiangqi',
        ],
        daap_caps)

    advertise_caps(q, bus, conn, service,
        [xiangqi_fixed_properties, no_service_fixed_properties],
        [ns.TP_FT_METADATA + '#com.example.Xiangqi'],
        [ns.TP_FT_METADATA + '#daap',
         ns.TP_FT_METADATA + '#http',
         ns.TP_FT_METADATA + '#com.example.Go',
        ],
        xiangqi_caps)

    advertise_caps(q, bus, conn, service,
        [xiangqi_fixed_properties, go_fixed_properties],
        [ns.TP_FT_METADATA + '#com.example.Xiangqi',
         ns.TP_FT_METADATA + '#com.example.Go',
        ],
        [ns.TP_FT_METADATA + '#http',
         ns.TP_FT_METADATA + '#daap',
        ],
        xiangqi_go_caps)

    advertise_caps(q, bus, conn, service,
        [go_fixed_properties],
        [ns.TP_FT_METADATA + '#com.example.Go',
        ],
        [ns.TP_FT_METADATA + '#http',
         ns.TP_FT_METADATA + '#daap',
         ns.TP_FT_METADATA + '#com.example.Xiangqi',
        ],
        go_caps)

def test(q, bus, conn):
    # last value of the "ver" key we resolved. We use it to be sure that the
    # modified caps has already be announced.
    old_ver = None

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 0])

    self_handle = conn.GetSelfHandle()
    self_handle_name =  conn.InspectHandles(1, [self_handle])[0]

    AvahiListener(q).listen_for_service("_presence._tcp")
    e = q.expect('service-added', name = self_handle_name,
        protocol = avahi.PROTO_INET)
    service = e.service
    service.resolve()

    e = q.expect('service-resolved', service = service)
    ver = txt_get_key(e.txt, "ver")
    while ver == old_ver:
        # be sure that the announced caps actually changes
        e = q.expect('service-resolved', service=service)
        ver = txt_get_key(e.txt, "ver")
    old_ver = ver

    caps = compute_caps_hash(['client/pc//%s' % PACKAGE_STRING],
        fixed_features, {})
    assertEquals(caps, ver)

    test_ft_caps_from_contact(q, bus, conn, service, 'yo@momma')

    test_ft_caps_to_contact(q, bus, conn, service)

if __name__ == '__main__':
    exec_test(test)
