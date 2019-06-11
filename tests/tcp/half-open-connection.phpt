--TEST--
TCP half open socket connection.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Task;
use Concurrent\Timer;

list ($a, $b) = TcpSocket::pair();

Task::async(function () use ($a) {
    try {
        var_dump($a->isAlive());
        
        while (null !== ($chunk = $a->read())) {
            var_dump($chunk);
        }
        
        var_dump($a->isAlive());
        
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
bool(true)
string(5) "Hello"
string(5) "World"
bool(false)
string(4) "DONE"
NULL
