<?php

namespace Concurrent\Network;

$socket = UdpSocket::bind('0.0.0.0', 0);

try {
    var_dump($socket->getAddress());
    
    $data = new UdpDatagram('Hello', '127.0.0.1', 12345);
    
    $socket->sendAsync($data);
    $socket->send($data->withData('World!'));
} finally {
    $socket->close();
}
