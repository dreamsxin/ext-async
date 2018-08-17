<?php

namespace Concurrent\Network;

$socket = TcpSocket::connect('google.com', 80);

try {
    var_dump($socket->getLocalPeer(), $socket->getRemotePeer());
    
    $socket->nodelay(true);
    $socket->write("GET / HTTP/1.0\r\nHost: google.com\r\nConnection: close\r\n\r\n");
    
    while (null !== ($chunk = $socket->read(256))) {
        var_dump($chunk);
    }
} finally {
    $socket->close();
}
