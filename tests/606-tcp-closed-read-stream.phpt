--TEST--
TCP closed read stream.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Task;
use Concurrent\Timer;

list ($a, $b) = TcpSocket::pair();

Task::async(function () use ($a) {
    try {
        var_dump($a->readStream()->read());
        $a->readStream()->close();
        
        $a->write('DONE');
    } finally {
        $a->close();
    }
});

try {
    $b->write('Hello');
    
    (new Timer(50))->awaitTimeout();
    
    var_dump($b->read());
} finally {
    $b->close();
}

--EXPECT--
string(5) "Hello"
string(4) "DONE"
