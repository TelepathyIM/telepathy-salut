import hashlib
import base64
from config import PACKAGE_STRING

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

def generate_caps(features):
    return compute_caps_hash(['client/pc//%s' % PACKAGE_STRING], features, [])

if __name__ == '__main__':
    # example from XEP-0115
    assert compute_caps_hash(['client/pc//Exodus 0.9.1'], ["http://jabber.org/protocol/disco#info",
        "http://jabber.org/protocol/disco#items", "http://jabber.org/protocol/muc",
        "http://jabber.org/protocol/caps"], []) == 'QgayPKawpkPSDYmwT/WM94uAlu0='
