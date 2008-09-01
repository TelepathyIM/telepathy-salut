from saluttest import exec_test
import avahitest
from avahitest import AvahiListener

import time

def test(q, bus, conn):
    a = AvahiListener(q)
    a.listen_for_service("_presence._tcp")

    conn.Connect()

    q.expect('service-added',
      name='test-register@' + avahitest.get_host_name())

if __name__ == '__main__':
    exec_test(test, { "published-name": "test-register" })
