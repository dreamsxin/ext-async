<?php

namespace Concurrent\Network;

if (($_SERVER['argv'][1] ?? null)) {
    $tls = new TlsServerEncryption();
    $tls = $tls->withDefaultCertificate(__DIR__ . '/cert/localhost.crt', __DIR__ . '/cert/localhost.key', 'localhost');
    $tls = $tls->withAlpnProtocols('foo/bar', 'test/1.0');
} else {
    $tls = null;
}

$server = TcpServer::listen('localhost', 8080, $tls);

try {
    var_dump($server->getAddress(), $server->getPort());
    
    for ($i = 0; $i < 3; $i++) {
        $socket = $server->accept();
        
        \Concurrent\Task::async(function () use ($socket, $tls) {
            var_dump('CLIENT CONNECTED');
            
            try {
                if ($tls) {
                    var_dump('Negotiate TLS');
                    var_dump($socket->encrypt());
                    var_dump('TLS established');
                }
                
                $buffer = '';
                
                while (null !== ($chunk = $socket->read())) {
                    $buffer .= $chunk;
                }
                
                var_dump($buffer);
                
                $socket->write('RECEIVED: ' . \strtoupper($buffer));
            } catch (\Throwable $e) {
                echo $e, "\n\n";
            } finally {
                $socket->close();
                
                var_dump('CLIENT DISCONNECTED');
            }
        });
    }
} finally {
    $server->close();
}
