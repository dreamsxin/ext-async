<?php

namespace Concurrent\Network;

$encryption = ($_SERVER['argv'][1] ?? null) ? new ClientEncryption() : null;

// $socket = TcpSocket::connect('google.com', $encryption ? 443 : 80, $encryption);
$socket = TcpSocket::connect('httpbin.org', $encryption ? 443 : 80, $encryption);

try {
    var_dump($socket->getLocalPeer(), $socket->getRemotePeer());
    
    $socket->setNodelay(true);
    
    if ($encryption) {
        $socket->encrypt();
    }
    
//     $socket->write("GET / HTTP/1.0\r\nHost: google.com\r\nConnection: close\r\n\r\n");
    $socket->write("GET /status/201 HTTP/1.0\r\nHost: httpbin.org\r\nConnection: close\r\n\r\n");
    
    while (null !== ($chunk = $socket->read())) {
        var_dump($chunk);
    }
} finally {
    $socket->close();
}
