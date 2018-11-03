<?php

namespace Concurrent\Network;

$tls = ($_SERVER['argv'][1] ?? null) ? new TlsClientEncryption() : null;

$socket = TcpSocket::connect('httpbin.org', $tls ? 443 : 80, $tls);

try {
    var_dump($socket->getAddress(), $socket->getPort());
    var_dump($socket->getRemoteAddress(), $socket->getRemotePort());
    
    $socket->setOption(TcpSocket::NODELAY, true);
    
    if ($tls) {
        $socket->encrypt();
    }
    
    $socket->write("GET /status/201 HTTP/1.0\r\nHost: httpbin.org\r\nConnection: close\r\n\r\n");
    
    while (null !== ($chunk = $socket->read())) {
        var_dump($chunk);
    }
} finally {
    $socket->close();
}
