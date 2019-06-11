--TEST--
UDP datagram properties are readonly.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Network;

$datagram = new UdpDatagram('Hello', '127.0.0.1', 1111);

try {
    $datagram->data = 'World';
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

--EXPECT--
string(65) "Cannot write to property "data" of Concurrent\Network\UdpDatagram"
