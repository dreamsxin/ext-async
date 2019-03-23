--TEST--
TCP half open socket connection.
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
        while (null !== ($chunk = $a->read())) {
            var_dump($chunk);
        }
        
        $a->write('DONE');
    } finally {
        $a->close();
    }
});

try {
    $c = $b->getWritableStream();
    $c->write('Hello');
    
    (new Timer(50))->awaitTimeout();
    
    $c->write('World');
    $c->close();
    
    var_dump($b->read(), $b->read());
} finally {
    $b->close();
}

--EXPECT--
string(5) "Hello"
string(5) "World"
string(4) "DONE"
NULL
