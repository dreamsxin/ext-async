--TEST--
UDP enforces remote address.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Task;

$a = UdpSocket::bind();
$b = UdpSocket::connect($a->getAddress(), $a->getPort());

try {
    $a->send(new UdpDatagram('foo'));
} catch (SocketException $e) {
    var_dump($e->getMessage());
}

try {
    Task::await(Task::async([$a, 'send'], new UdpDatagram('foo')));
} catch (SocketException $e) {
    var_dump($e->getMessage());
}

try {
    $b->send(new UdpDatagram('foo', $a->getAddress(), $a->getPort()));
} catch (SocketException $e) {
    var_dump($e->getMessage());
}

try {
    Task::await(Task::async([$b, 'send'], new UdpDatagram('foo', $a->getAddress(), $a->getPort())));
} catch (SocketException $e) {
    var_dump($e->getMessage());
}

--EXPECT--
string(48) "Unconnected UDP socket requires a target address"
string(48) "Unconnected UDP socket requires a target address"
string(55) "Connected UDP socket cannot send to a different address"
string(55) "Connected UDP socket cannot send to a different address"
