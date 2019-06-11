--TEST--
TCP detects writes to broken pipe.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Stream\StreamException;

list ($a, $b) = TcpSocket::pair();

$a->close();

var_dump('START');

$b->setOption(TcpSocket::NODELAY, true);

try {
    $b->write(str_repeat('A', 1024 * 1024 * 4));
} catch (StreamException $e) {
    var_dump(substr($e->getMessage(), 0, 23));
}

var_dump('DONE');

--EXPECT--
string(5) "START"
string(23) "Write operation failed:"
string(4) "DONE"
