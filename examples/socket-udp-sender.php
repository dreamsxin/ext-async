<?php

namespace Concurrent\Network;

use Concurrent\Task;

$socket = UdpSocket::bind();

try {
    var_dump($socket->getAddress());
    
    $data = new UdpDatagram('Hello', '127.0.0.1', 12345);
    
    Task::async([
        $socket,
        'send'
    ], $data);
    
    $socket->send($data->withData('World!'));
} finally {
    $socket->close();
}
