<?php

namespace Concurrent\Network;

if ($_SERVER['argv'][1] ?? null) {
    $encryption = new ClientEncryption();
    $encryption = $encryption->withAllowSelfSigned(true);
} else {
    $encryption = null;
}

$socket = TcpSocket::connect('localhost', 8080, $encryption);

try {
    if ($encryption) {
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
