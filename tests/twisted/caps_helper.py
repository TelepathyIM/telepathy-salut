# vim: set fileencoding=utf-8 :
import hashlib
import base64
import dbus

from avahitest import txt_get_key
from twisted.words.xish import domish, xpath
from saluttest import make_result_iq, make_presence, elem_iq, elem
from servicetest import (
    EventPattern,
    assertEquals, assertContains, assertDoesNotContain, assertLength,
    )

import config
import ns
import constants as cs

# The caps we always have, regardless of any clients' caps
FIXED_CAPS = [
    ns.JINGLE,
    ns.JINGLE_015,
    ns.GOOGLE_FEAT_SESSION,
    ns.JINGLE_TRANSPORT_RAWUDP,
    ns.NICK,
    ns.NICK + '+notify',
    ns.CHAT_STATES,
    ns.SI,
    ns.IBB,
    ns.BYTESTREAMS,
    ]

JINGLE_CAPS = [
    # Additional Jingle transports
    ns.JINGLE_TRANSPORT_ICEUDP,
    ns.GOOGLE_P2P,
    # Jingle content types
    ns.GOOGLE_FEAT_VOICE,
    ns.GOOGLE_FEAT_VIDEO,
    ns.JINGLE_015_AUDIO,
    ns.JINGLE_015_VIDEO,
    ns.JINGLE_RTP,
    ns.JINGLE_RTP_AUDIO,
    ns.JINGLE_RTP_VIDEO,
    ]

VARIABLE_CAPS = (
    JINGLE_CAPS +
    [
    ns.FILE_TRANSFER,

    # FIXME: currently we always advertise these, but in future we should
    # only advertise them if >= 1 client supports them:
    # ns.TUBES,

    # there is an unlimited set of these; only the ones actually relevant to
    # the tests so far are shown here
    ns.TUBES + '/stream#x-abiword',
    ns.TUBES + '/stream#daap',
    ns.TUBES + '/stream#http',
    ns.TUBES + '/dbus#com.example.Go',
    ns.TUBES + '/dbus#com.example.Xiangqi',
    ])

def check_caps(namespaces, desired):
    """Assert that all the FIXED_CAPS are supported, and of the VARIABLE_CAPS,
    every capability in desired is supported, and every other capability is
    not.
    """
    for c in FIXED_CAPS:
        assertContains(c, namespaces)

    for c in VARIABLE_CAPS:
        if c in desired:
            assertContains(c, namespaces)
        else:
            assertDoesNotContain(c, namespaces)

# taken from salut's old caps_helper.py
def check_caps_txt(txt, ver):
    for (key, val) in { "1st": "test",
                        "last": "suite",
                        "status": "avail",
                        "txtvers": "1" }.iteritems():
        v =  txt_get_key(txt, key)
        assert v == val, (key, val, v)

    assert txt_get_key(txt, "hash") == "sha-1"
    assert txt_get_key(txt, "node") == ns.GABBLE_CAPS

    v = txt_get_key(txt, "ver")
    assert v == ver, (v, ver)

text_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT
    })
text_allowed_properties = dbus.Array([cs.TARGET_HANDLE])

stream_tube_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAM_TUBE
    })
stream_tube_allowed_properties = dbus.Array([cs.TARGET_HANDLE,
    cs.TARGET_ID, cs.STREAM_TUBE_SERVICE])

dbus_tube_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_DBUS_TUBE
    })
dbus_tube_allowed_properties = dbus.Array([cs.TARGET_HANDLE,
    cs.TARGET_ID, cs.DBUS_TUBE_SERVICE_NAME])

ft_fixed_properties = dbus.Dictionary({
    cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
    cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_FILE_TRANSFER,
    })
ft_allowed_properties = dbus.Array([
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.ContentHashType',
    cs.TARGET_HANDLE,
    cs.TARGET_ID,
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.ContentType',
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.Filename',
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.Size',
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.ContentHash',
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.Description',
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.Date',
    cs.CHANNEL_TYPE_FILE_TRANSFER + '.InitialOffset',
    cs.FT_URI])
ft_allowed_properties_with_metadata = ft_allowed_properties + [
    cs.FT_SERVICE_NAME,
    cs.FT_METADATA]

fake_client_dataforms = {
    'urn:xmpp:dataforms:softwareinfo':
    {'software': ['A Fake Client with Twisted'],
        'software_version': ['5.11.2-svn-20080512'],
        'os': ['Debian GNU/Linux unstable (sid) unstable sid'],
        'os_version': ['2.6.24-1-amd64'],
    },
}

def compute_caps_hash(identities, features, dataforms):
    """
    Accepts a list of slash-separated identities, a list of feature namespaces,
    and a map from FORM_TYPE to (map from field name to values), returns the
    verification string as defined by
    <http://xmpp.org/extensions/xep-0115.html#ver>.
    """
    components = []

    for identity in sorted(identities):
        if len(identity.split('/')) != 4:
            raise ValueError(
                "expecting identities of the form " +
                "'category/type/lang/client': got " + repr(identity))

        components.append(identity)

    for feature in sorted(features):
        components.append(feature)

    for form_type in sorted(dataforms.keys()):
        components.append(form_type)

        for var in sorted(dataforms[form_type].keys()):
            components.append(var)

            for value in sorted(dataforms[form_type][var]):
                components.append(value)

    components.append('')

    m = hashlib.sha1()
    S = u'<'.join(components)
    m.update(S.encode('utf-8'))
    return base64.b64encode(m.digest())

def make_caps_disco_reply(stream, req, identities, features, dataforms={}):
    iq = make_result_iq(req)
    query = iq.firstChildElement()

    for identity in identities:
        category, type_, lang, name = identity.split('/')
        el = query.addElement('identity')
        el['category'] = category
        el['type'] = type_
        el['name'] = name

    for f in features:
        el = domish.Element((None, 'feature'))
        el['var'] = f
        query.addChild(el)

    add_dataforms(query, dataforms)

    return iq

def add_dataforms(query, dataforms):
    for type, fields in dataforms.iteritems():
        x = query.addElement((ns.X_DATA, 'x'))
        x['type'] = 'result'

        field = x.addElement('field')
        field['var'] = 'FORM_TYPE'
        field['type'] = 'hidden'
        field.addElement('value', content=type)

        for var, values in fields.iteritems():
            field = x.addElement('field')
            field['var'] = var

            for value in values:
                field.addElement('value', content=value)

def receive_presence_and_ask_caps(q, stream, expect_dbus=True):
    # receive presence stanza
    if expect_dbus:
        presence, event_dbus = q.expect_many(
                EventPattern('stream-presence'),
                EventPattern('dbus-signal', signal='ContactCapabilitiesChanged')
            )
        assertLength(1, event_dbus.args)
        signaled_caps = event_dbus.args[0]
    else:
        presence = q.expect('stream-presence')
        signaled_caps = None

    return disco_caps(q, stream, presence) + (signaled_caps,)

def extract_data_forms(x_nodes):
    dataforms = {}

    if not x_nodes:
        return dataforms

    for form in x_nodes:
        name = None
        fields = {}
        for field in xpath.queryForNodes('/x/field', form):
            if field['var'] == 'FORM_TYPE':
                name = str(field.firstChildElement())
            else:
                values = [str(x) for x in xpath.queryForNodes('/field/value', field)]

                fields[field['var']] = values

        if name is not None:
            dataforms[name] = fields

    return dataforms

def disco_caps(q, stream, txt):
    hash = txt_get_key(txt, 'hash')
    ver = txt_get_key(txt, 'ver')
    node = txt_get_key(txt, 'node')
    assertEquals('sha-1', hash)

    # ask caps
    request = \
        elem_iq(stream, 'get', from_='fake_contact@nearby')(
          elem(ns.DISCO_INFO, 'query', node=(node + '#' + ver))
        )
    stream.send(request)

    # receive caps
    event = q.expect('stream-iq', query_ns=ns.DISCO_INFO, iq_id=request['id'])

    # Check that Gabble's announcing the identity we think it should be.
    identity_nodes = xpath.queryForNodes('/iq/query/identity', event.stanza)
    assertLength(1, identity_nodes)
    identity_node = identity_nodes[0]

    assertEquals('client', identity_node['category'])
    assertEquals('pc', identity_node['type'])
    assertEquals(config.PACKAGE_STRING, identity_node['name'])
    assertDoesNotContain('xml:lang', identity_node.attributes)

    identity = 'client/%s//%s' % ('pc', config.PACKAGE_STRING)

    features = []
    for feature in xpath.queryForNodes('/iq/query/feature', event.stanza):
        features.append(feature['var'])

    # Check if the hash matches the announced capabilities
    assertEquals(compute_caps_hash([identity], features, {}), ver)

    return (event, features)

def caps_contain(event, cap):
    node = xpath.queryForNodes('/iq/query/feature[@var="%s"]'
            % cap,
            event.stanza)
    if node is None:
        return False
    if len(node) != 1:
        return False
    var = node[0].attributes['var']
    if var is None:
        return False
    return var == cap

def presence_and_disco(q, conn, stream, contact, disco,
                       client, caps,
                       features, identities=[], dataforms={},
                       initial=True, show=None):
    h = send_presence(q, conn, stream, contact, caps, initial=initial,
        show=show)

    if disco:
        stanza = expect_disco(q, contact, client, caps)
        send_disco_reply(stream, stanza, identities, features, dataforms)

    return h

def send_presence(q, conn, stream, contact, caps, initial=True, show=None):
    h = conn.RequestHandles(cs.HT_CONTACT, [contact])[0]

    if initial:
        stream.send(make_presence(contact, status='hello'))

        q.expect_many(
            EventPattern('dbus-signal', signal='PresenceUpdate',
                args=[{h:
                   (0L, {u'available': {'message': 'hello'}})}]),
            EventPattern('dbus-signal', signal='PresencesChanged',
                args=[{h:
                   (2, u'available', 'hello')}]))

        # no special capabilities
        assertEquals([(h, cs.CHANNEL_TYPE_TEXT, 3, 0)],
            conn.Capabilities.GetCapabilities([h]))

    # send updated presence with caps info
    stream.send(make_presence(contact, show=show, status='hello', caps=caps))

    return h

def expect_disco(q, contact, client, caps):
    # Gabble looks up our capabilities
    event = q.expect('stream-iq', to=contact, query_ns=ns.DISCO_INFO)
    assertEquals(client + '#' + caps['ver'], event.query['node'])

    return event.stanza

def send_disco_reply(stream, stanza, identities, features, dataforms={}):
    stream.send(
        make_caps_disco_reply(stream, stanza, identities, features, dataforms))

if __name__ == '__main__':
    # example from XEP-0115
    assertEquals('QgayPKawpkPSDYmwT/WM94uAlu0=',
        compute_caps_hash(['client/pc//Exodus 0.9.1'],
            ["http://jabber.org/protocol/disco#info",
             "http://jabber.org/protocol/disco#items",
             "http://jabber.org/protocol/muc",
             "http://jabber.org/protocol/caps"],
            {}))

    # another example from XEP-0115
    identities = [u'client/pc/en/Psi 0.11', u'client/pc/el/Ψ 0.11']
    features = [
        u'http://jabber.org/protocol/caps',
        u'http://jabber.org/protocol/disco#info',
        u'http://jabber.org/protocol/disco#items',
        u'http://jabber.org/protocol/muc',
        ]
    dataforms = {
        u'urn:xmpp:dataforms:softwareinfo':
            { u'ip_version': [u'ipv4', u'ipv6'],
              u'os': [u'Mac'],
              u'os_version': [u'10.5.1'],
              u'software': [u'Psi'],
              u'software_version': [u'0.11'],
            },
        }
    assertEquals('q07IKJEyjvHSyhy//CH0CxmKi8w=',
        compute_caps_hash(identities, features, dataforms))
