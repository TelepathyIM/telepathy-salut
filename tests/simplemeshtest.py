#!/usr/bin/env python

from twisted.internet import reactor
from mesh import Mesh
import sys

NUMNODES = 5
NUMPACKETS = 10


nodes = []
# We're optimists
success = True

class TestMesh(Mesh):
  expected = {}
  done = 0

  def connected(self, node):
    if node == self.nodes[0]:
      for x in xrange(0, NUMPACKETS):
        reactor.callLater(1.0 * x,
          (lambda y: node.pushInput(str(y) + "\n")), x)


  def gotOutput(self, node, sender, data):
    global success

    if self.expected.get(node) == None:
      self.expected[node] = int(data)

    if (self.expected.get(node, int(data)) != int(data)):
      print "Got " + data.rstrip() + " instead of " + \
             str(self.expected[node]) + " from "  + node.name

      success = False
      reactor.crash()

    if not sender in node.peers:
      print "Sender " + sender + " not in node peers"
      success = False
      reactor.crash()

    self.expected[node] = int(data) + 1

    if self.expected[node] == 10:
      self.done += 1

    if self.done == NUMNODES - 1:
      reactor.stop()

m = TestMesh()

for x in xrange(0, NUMNODES):
  nodes.append(m.addNode("node" + str(x)))

# Connect all nodes to all others. 1024 bytes/s bandwidth, 50ms delay and 0%
# packet loss.. (bandwidth and delay aren't implemented just yet)
m.connect_full(1024, 50, 0)

def timeout():
  global success
  print "TIMEOUT!"
  success = False
  reactor.crash()

reactor.callLater(60, timeout)

reactor.run()


if not success:
  print "FAILED"
  sys.exit(-1)

print "SUCCESS"
