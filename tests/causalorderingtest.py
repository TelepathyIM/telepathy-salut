#!/usr/bin/env python

from twisted.internet import reactor
from mesh import Mesh
import sys

nodes = []
# We're optimists
success = True

class TestMesh(Mesh):
  done = 0
  expected = None

  def gotOutput(self, node, sender, data):
    global success
    value = int(data.rstrip())
    if (node in self.nodes[0:2] and 
        reduce((lambda v, n: v or sender == n.name), self.nodes[0:2], False)):
      reactor.callLater(0.1, (lambda: node.pushInput( str(value + 1) +  "\n")))
    else:
      print node.name + " - " + sender + " - " + data.rstrip()
      if self.expected == None:
        self.expected = value
      if self.expected > value:
        print "Expected: " + str(self.expected) + " But got: " + str(value)
        success = False
        reactor.crash()
      self.expected = value + 1

m = TestMesh()

for x in xrange(0, 3):
  nodes.append(m.addNode("node" + str(x)))

#connect node 0 and 1 together with dropfree links, and  0 <->2 and 1 <-> 2 
# with quite lossy links
m.connect_duplex(nodes[0], nodes[1], 100, 0, 0)
m.connect_duplex(nodes[0], nodes[2], 100, 0, 0.50)
m.connect_duplex(nodes[1], nodes[2], 100, 0, 0.50)

reactor.callLater(0.1, (lambda y: nodes[0].pushInput("0\n")), x)

#def timeout():
#  global success
#  print "TIMEOUT!"
#  success = False
#  reactor.crash()

#reactor.callLater(60, timeout)

reactor.run()


if not success:
  print "FAILED"
  sys.exit(-1)

print "SUCCESS"
