<?php

namespace Concurrent\Network;

use Concurrent\Task;

Task::async('count', []);

$tls = ($_SERVER['argv'][1] ?? null) ? new TlsClientEncryption() : null;

if ($tls) {
    $tls = $tls->withAlpnProtocols('foo/bar', 'http/1.1');
}

$socket = TcpSocket::connect('httpbin.org', $tls ? 443 : 80, $tls);

try {
    var_dump($socket->getAddress(), $socket->getPort());
    var_dump($socket->getRemoteAddress(), $socket->getRemotePort());
    var_dump($socket->isAlive());

    var_dump($socket->setOption(TcpSocket::NODELAY, true));

    if ($tls) {
        var_dump($socket->encrypt());
    }

    // IS: async $socket->write("GET /json HTTP/1.0\r\nHost: httpbin.org\r\nConnection: close\r\n\r\n");
    Task::async([
        $socket,
        'write'
    ], "GET /json HTTP/1.0\r\nHost: httpbin.org\r\n");
    
    $x = Task::async([
        $socket,
        'write'
    ], "Connection: close\r\n\r\n");
    
    var_dump($x);
    
    var_dump($socket->getWriteQueueSize());
    var_dump(Task::await($x));
    
    while (null !== ($chunk = $socket->read())) {
        var_dump($chunk);
    }
    
    var_dump($socket->isAlive());
} finally {
    $socket->close();
}

var_dump($socket->isAlive());
