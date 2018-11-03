<?php

namespace Concurrent\Network;

if ($_SERVER['argv'][1] ?? null) {
    $tls = new TlsClientEncryption();
    $tls = $tls->withAllowSelfSigned(true);
} else {
    $tls = null;
}

$socket = TcpSocket::connect('localhost', 8080, $tls);

try {
    if ($tls) {
        var_dump('Negotiate TLS');
        $socket->encrypt();
        var_dump('TLS established');
    }
    
    $socket->write('Hello World :)');
    $socket->writeStream()->close();
    
    while (null !== ($chunk = $socket->read())) {
        var_dump($chunk);
    }
} finally {
    $socket->close();
}
