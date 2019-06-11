--TEST--
UDP async send operations.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Context;
use Concurrent\Task;
use Concurrent\Timer;

$a = UdpSocket::bind('127.0.0.1', 0);

Task::async(function () use ($a) {
    $b = UdpSocket::bind('127.0.0.1', 0);

    try {
        $timer = new Timer(80);
        
        for ($i = 0; $i < 3; $i++) {
            $timer->awaitTimeout();
            
            Task::async([$b, 'send'], new UdpDatagram((string) $i, $a->getAddress(), $a->getPort()));
        }
        
        $b->flush();
    } finally {
        $b->close();
    }
});

try {
    for ($i = 0; $i < 3; $i++) {
        var_dump((int) $a->receive()->data);
    }
} finally {
    $a->close();
}

--EXPECT--
int(0)
int(1)
int(2)
