--TEST--
TCP connected socket pair with large payload.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Task;

list ($a, $b) = TcpSocket::pair();

$len = 7000;
$count = 100;

Task::async(function (TcpSocket $socket) use ($len, $count) {
    try {
        $chunk = str_repeat('A', $len);

        for ($i = 0; $i < $count; $i++) {
            $socket->write($chunk);
        }
    } finally {
        $socket->close();
    }
}, $a);

$received = 0;

try {
    while (null !== ($chunk = $b->read())) {
        $received += strlen($chunk);
    }
} finally {
    $b->close();
}

var_dump($received == ($len * $count));

--EXPECT--
bool(true)
