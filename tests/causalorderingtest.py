#!/usr/bin/env python

from twisted.internet import reactor
from mesh import Mesh
from mesh import MeshNode
import sys

nodes = []
# We're optimists
success = True

class TestMeshNode(MeshNode):
  def unknownOutput(self, line):
    if (self.name == "node3"):
      MeshNode.unknownOutput(self, line)

class TestMesh(Mesh):
  done = 0
  expected = None

  def connected(self, node):
    if node == self.nodes[0]:
      reactor.callLater(1.5, (lambda y: node.pushInput("0\n")), x)

  def gotOutput(self, node, sender, data):
    global success
    value = int(data.rstrip())
    if (node in self.nodes[0:3]):
      if (self.nodes.index(node) == (value  + 1) % 3):
        reactor.callLater(0.1, 
            (lambda: node.pushInput( str(value + 1) +  "\n")))
    else:
      print node.name + " - " + sender + " - " + data.rstrip()
      if self.expected == None:
        self.expected = value
      if self.expected > 2 and self.expected != value:
        print "Expected: " + str(self.expected) + " But got: " + str(value)
        success = False
        reactor.crash()
      self.expected = value + 1

      if self.expected > 50:
        reactor.crash()

m = TestMesh()

for x in xrange(0, 4):
  n = TestMeshNode("node" + str(x), m)
  nodes.append(n)
  m.addMeshNode(n)

#connect node 0 and 1 together with dropfree links, and  0 <->2 and 1 <-> 2 
# with quite lossy links
m.connect_duplex(nodes[0], nodes[1], 100, 0, 0)
m.connect_duplex(nodes[0], nodes[2], 100, 0, 0)

m.connect_duplex(nodes[1], nodes[2], 100, 0, 0)

m.connect_duplex(nodes[0], nodes[3], 100, 0, 0.50)
m.connect_duplex(nodes[1], nodes[3], 100, 0, 0.50)
m.connect_duplex(nodes[2], nodes[3], 100, 0, 0.50)

def timeout():
  global success
  print "TIMEOUT!"
  success = False
  reactor.crash()

reactor.callLater(60, timeout)

reactor.run()


if not success:
  sys.stderr.write("FAILED\n")
  sys.exit(-1)

print "SUCCESS"
