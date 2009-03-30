import hashlib
import base64

from avahitest import txt_get_key
import ns

def compute_caps_hash(identities, features, dataforms):
    S = ''

    for identity in sorted(identities):
        S += '%s<' % identity

    for feature in sorted(features):
        S += '%s<' % feature

    # FIXME: support dataforms

    m = hashlib.sha1()
    m.update(S)
    return base64.b64encode(m.digest())

def check_caps(txt, ver):
    for (key, val) in { "1st": "test",
                        "last": "suite",
                        "status": "avail",
                        "txtvers": "1" }.iteritems():
        v =  txt_get_key(txt, key)
        assert v == val, (key, val, v)

    assert txt_get_key(txt, "hash") == "sha-1"
    assert txt_get_key(txt, "node") == ns.TELEPATHY_CAPS

    v = txt_get_key(txt, "ver")
    assert v == ver, (v, ver)

if __name__ == '__main__':
    # example from XEP-0115
    assert compute_caps_hash(['client/pc//Exodus 0.9.1'],
        ["http://jabber.org/protocol/disco#info",
        "http://jabber.org/protocol/disco#items",
        "http://jabber.org/protocol/muc", "http://jabber.org/protocol/caps"],
        []) == 'QgayPKawpkPSDYmwT/WM94uAlu0='
