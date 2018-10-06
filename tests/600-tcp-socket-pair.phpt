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
string(5) "Hello"
string(6) "World!"
