--TEST--
UDP unicast send and receive.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Task;
use Concurrent\Timer;

$a = UdpSocket::bind('localhost', 0);
$b = UdpSocket::bind('127.0.0.1', 0);

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
    $b->send(new UdpDatagram('Test', '127.0.0.1', $a->getPeer()[1]));
    
    (new Timer(50))->awaitTimeout();
    
    $data = $b->receive();
    
    var_dump(isset($data->data));
    var_dump(!empty($data->data));
    var_dump(isset($data->foo));
    
    var_dump(@$data->foo);
    
    var_dump($data->data);
    var_dump($data->address);
    var_dump($data->__debugInfo()['port'] == $a->getPort());
} finally {
    $b->close();
}

$data = $data->withPeer('127.0.0.2', 8080);

var_dump($data->address);
var_dump($data->port);

--EXPECT--
string(4) "Test"
string(9) "127.0.0.1"
bool(true)
bool(true)
bool(true)
bool(false)
NULL
string(9) "RECEIVED!"
string(9) "127.0.0.1"
bool(true)
string(9) "127.0.0.2"
int(8080)
