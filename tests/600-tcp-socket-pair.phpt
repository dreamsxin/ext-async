--TEST--
TCP connected socket pair.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Task;

list ($a, $b) = TcpSocket::pair();

var_dump($a instanceof TcpSocket);
var_dump($b instanceof TcpSocket);

var_dump($a->getLocalPeer());
var_dump($a->getRemotePeer());

Task::async(function () use ($a) {
    try {
        var_dump($a->read());
        
        $a->write('World!');
    } finally {
        $a->close();
    }
});

try {
    $b->write('Hello');
    
    while (null !== ($chunk = $b->read())) {
        var_dump($chunk);
    }
} finally {
    $b->close();
}

--EXPECT--
bool(true)
bool(true)
array(2) {
  [0]=>
  string(9) "127.0.0.1"
  [1]=>
  int(0)
}
array(2) {
  [0]=>
  string(9) "127.0.0.1"
  [1]=>
  int(0)
}
string(5) "Hello"
string(6) "World!"
