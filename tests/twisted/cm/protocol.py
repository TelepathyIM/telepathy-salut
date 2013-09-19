"""
Test Salut's o.fd.T.Protocol implementation
"""

import dbus
from servicetest import (unwrap, tp_path_prefix, assertEquals, assertContains,
        call_async)
from saluttest import exec_test
from avahitest import AvahiListener
import constants as cs

def test(q, bus, conn):
    cm = bus.get_object(cs.CM + '.salut',
        tp_path_prefix + '/ConnectionManager/salut')
    cm_iface = dbus.Interface(cm, cs.CM)
    cm_prop_iface = dbus.Interface(cm, cs.PROPERTIES_IFACE)

    protocols = unwrap(cm_prop_iface.Get(cs.CM, 'Protocols'))
    assertEquals(set(['local-xmpp']), set(protocols.keys()))

    protocol_names = unwrap(cm_iface.ListProtocols())
    assertEquals(set(['local-xmpp']), set(protocol_names))

    cm_params = cm_iface.GetParameters('local-xmpp')
    local_props = protocols['local-xmpp']
    local_params = local_props[cs.PROTOCOL + '.Parameters']
    assertEquals(cm_params, local_params)

    proto = bus.get_object(cm.bus_name, cm.object_path + '/local_xmpp')
    proto_iface = dbus.Interface(proto, cs.PROTOCOL)
    proto_prop_iface = dbus.Interface(proto, cs.PROPERTIES_IFACE)
    proto_props = unwrap(proto_prop_iface.GetAll(cs.PROTOCOL))

    for key in ['Parameters', 'Interfaces', 'ConnectionInterfaces',
      'RequestableChannelClasses', u'VCardField', u'EnglishName', u'Icon']:
        a = local_props[cs.PROTOCOL + '.' + key]
        b = proto_props[key]
        assertEquals(a, b)

    assertEquals('', proto_props['VCardField'])
    assertEquals('Link-local XMPP', proto_props['EnglishName'])
    assertEquals('im-local-xmpp', proto_props['Icon'])

    assertContains(cs.CONN_IFACE_ALIASING, proto_props['ConnectionInterfaces'])
    assertContains(cs.CONN_IFACE_AVATARS, proto_props['ConnectionInterfaces'])
    assertContains(cs.CONN_IFACE_CONTACTS, proto_props['ConnectionInterfaces'])
    assertContains(cs.CONN_IFACE_SIMPLE_PRESENCE,
            proto_props['ConnectionInterfaces'])
    assertContains(cs.CONN_IFACE_REQUESTS, proto_props['ConnectionInterfaces'])

    # local-xmpp has case-sensitive literals as identifiers
    assertEquals('SMcV@Reptile',
        unwrap(proto_iface.NormalizeContact('SMcV@Reptile')))

    # (Only) 'first-name' and 'last-name' are mandatory for IdentifyAccount()
    call_async(q, proto_iface, 'IdentifyAccount', {'first-name': 'Simon'})
    q.expect('dbus-error', method='IdentifyAccount', name=cs.INVALID_ARGUMENT)

    # Identifying an account doesn't do much, anyway
    test_params = {'first-name': 'Simon', 'last-name': 'McVittie'}
    acc_name = unwrap(proto_iface.IdentifyAccount(test_params))
    assertEquals('', acc_name)

    assertContains(cs.PROTOCOL_IFACE_AVATARS, proto_props['Interfaces'])
    avatar_props = unwrap(proto_prop_iface.GetAll(cs.PROTOCOL_IFACE_AVATARS))
    assertEquals(65535, avatar_props['MaximumAvatarBytes'])
    assertEquals(0, avatar_props['MaximumAvatarHeight'])
    assertEquals(0, avatar_props['MaximumAvatarWidth'])
    assertEquals(0, avatar_props['MinimumAvatarHeight'])
    assertEquals(0, avatar_props['MinimumAvatarWidth'])
    assertEquals(64, avatar_props['RecommendedAvatarHeight'])
    assertEquals(64, avatar_props['RecommendedAvatarWidth'])
    assertEquals(['image/png', 'image/jpeg'], avatar_props['SupportedAvatarMIMETypes'])

    assertContains(cs.PROTOCOL_IFACE_PRESENCES, proto_props['Interfaces'])

    expected_status = {'available': (cs.PRESENCE_AVAILABLE,     True,  True),
                       'dnd'      : (cs.PRESENCE_BUSY,          True,  True),
                       'away'     : (cs.PRESENCE_AWAY,          True,  True),
                       'offline'  : (cs.PRESENCE_OFFLINE,       False, False)}

    presences = proto_prop_iface.Get(cs.PROTOCOL_IFACE_PRESENCES, 'Statuses');
    assertEquals(expected_status, presences)

if __name__ == '__main__':
    exec_test(test)
