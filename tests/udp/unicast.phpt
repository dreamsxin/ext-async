--TEST--
UDP unicast send and receive.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Task;
use Concurrent\Timer;

$a = UdpSocket::bind();
$b = UdpSocket::bind('127.0.0.1', 0);

var_dump($a->getAddress());

Task::async(function () use ($a, $b) {
    try {
        $data = $a->receive();
        
        var_dump($data->data);
        var_dump($data->address);
        var_dump($data->port == $b->getPort());
        
        $a->send($data->withData('RECEIVED!'));
    } finally {
        $a->close();
    }
});

try {
    $b->send(new UdpDatagram('Test', '127.0.0.1', $a->getPort()));
    
    (new Timer(50))->awaitTimeout();
    
    $data = $b->receive();
    
    var_dump(isset($data->data));
    var_dump(!empty($data->data));
    var_dump(isset($data->foo));
    
    var_dump(@$data->foo);
    
    print_r($data);
} finally {
    $b->close();
}

$data = $data->withPeer('127.0.0.2', 8080);

var_dump($data->address);
var_dump($data->port);

--EXPECTF--
string(7) "0.0.0.0"
string(4) "Test"
string(9) "127.0.0.1"
bool(true)
bool(true)
bool(true)
bool(false)
NULL
Concurrent\Network\UdpDatagram Object
(
    [data] => RECEIVED!
    [address] => 127.0.0.1
    [port] => %d
)
string(9) "127.0.0.2"
int(8080)
