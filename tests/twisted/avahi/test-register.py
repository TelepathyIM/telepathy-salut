from saluttest import exec_test
import avahitest
from avahitest import AvahiListener
from avahitest import txt_get_key
from avahi import txt_array_to_string_array

import time

PUBLISHED_NAME="test-register"
FIRST_NAME="lastname"
LAST_NAME="lastname"

def test(q, bus, conn):
    a = AvahiListener(q)
    a.listen_for_service("_presence._tcp")

    conn.Connect()

    e = q.expect('service-added',
      name=PUBLISHED_NAME + "@" + avahitest.get_host_name())

    service = a.resolver_for_service(e)

    e = q.expect('service-resolved', service = service)

    for (key, val) in { "1st": FIRST_NAME,
                        "last": LAST_NAME,
                        "status": "avail",
                        "txtvers": "1" }.iteritems():
        v =  txt_get_key(e.txt, key)
        assert v == val, (key, val, v)

if __name__ == '__main__':
    exec_test(test, { "published-name": PUBLISHED_NAME,
                      "first-name": FIRST_NAME,
                      "last-name": LAST_NAME })
