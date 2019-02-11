<?php

namespace Concurrent\Network;

if ($_SERVER['argv'][1] ?? null) {
    $tls = new TlsClientEncryption();
    $tls = $tls->withAllowSelfSigned(true);
    $tls = $tls->withAlpnProtocols('test/1.1', 'test/1.0');
} else {
    $tls = null;
}

$socket = TcpSocket::connect('localhost', 8080, $tls);

try {
    if ($tls) {
        var_dump('Negotiate TLS');
        var_dump($socket->encrypt());
        var_dump('TLS established');
    }
    
    $socket->write('Hello World :)');
    $socket->getWritableStream()->close();
    
    while (null !== ($chunk = $socket->read())) {
        var_dump($chunk);
    }
} finally {
    $socket->close();
}
