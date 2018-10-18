<?php

namespace Concurrent\Network;

$socket = UdpSocket::bind('127.0.0.1', 12345);

try {
    for ($i = 0; $i < 6; $i++) {
        $datagram = $socket->receive();
        
        printf("RECEIVED >> %s (%s:%u)\n", $datagram->data, $datagram->address, $datagram->port);
    }
} finally {
    $socket->close();
}
