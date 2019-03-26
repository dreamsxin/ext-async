<?php

namespace Concurrent\Network;

use Concurrent\Timer;

/*
$context = stream_context_create([
    'ssl' => [
        'local_cert' => dirname(__DIR__) . '/cert/localhost.pem',
        'passphrase' => 'localhost'
    ]
]);

$errno = null;
$errstr = null;

$server = stream_socket_server('tls://127.0.0.1:10011', $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN, $context);
$sock = stream_socket_accept($server);

while (!feof($sock)) {
    $chunk = fread($sock, 0xFFFF);
    
    if ($chunk !== false) {
        if ($chunk !== str_repeat('A', strlen($chunk))) {
            throw new \Error('Corrupted data received');
        }
        
        $len += strlen($chunk);
    } else {
        break;
    }
}

var_dump($len);

return;
*/

$file = dirname(__DIR__) . '/cert/localhost.';

$tls = new TlsServerEncryption();
$tls = $tls->withDefaultCertificate($file . 'crt', $file . 'key', 'localhost');

$server = TcpServer::listen('localhost', 10011, $tls);

$sock = $server->accept();

if (!empty($_SERVER['argv'][1])) {
    $sock->encrypt();
}

try {
    $timer = new Timer(2);
    $len = 0;

    while (null !== ($chunk = $sock->read())) {
        $timer->awaitTimeout();
        $len += strlen($chunk);

        if ($chunk !== str_repeat('A', strlen($chunk))) {
            throw new \Error('Corrupted data received');
        }
    }

    var_dump($len);
} catch (\Throwable $e) {
    echo $e, "\n\n";
} finally {
    $sock->close();
}
