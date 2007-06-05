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

  def gotOutput(self, node, data):
    if self.expected.get(node) == None:
      self.expected[node] = int(data)

    if (self.expected.get(node, int(data)) != int(data)):
      global success
      print "Got " + data.rstrip() + " instead of " + \
             str(self.expected[node]) + " from "  + node.name

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

# Connect all nodes to all others. 1024 bytes/s bandwidth, 50ms delay and 50%
# packet loss.. (bandwidth and delay aren't implemented just yet)
m.connect_full(1024, 50, 0.50)

firstnode = nodes[0]
for x in xrange(0, NUMPACKETS):
  firstnode.pushInput(str(x) + "\n")

reactor.run()

if not success:
  print "FAILED"
  sys.exit(-1)

print "SUCCESS"
