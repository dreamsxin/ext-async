<?php

namespace Concurrent\Network;

if (($_SERVER['argv'][1] ?? null)) {
    $encryption = new ServerEncryption();
    $encryption = $encryption->withDefaultCertificate(__DIR__ . '/cert/localhost.crt', __DIR__ . '/cert/localhost.key', 'localhost');
} else {
    $encryption = null;
}

$server = TcpServer::listen('localhost', 8080, $encryption);

try {
    var_dump($server->getHost(), $server->getPort(), $server->getPeer());
    
    for ($i = 0; $i < 3; $i++) {
        $socket = $server->accept();
        
        \Concurrent\Task::async(function () use ($socket, $encryption) {
            var_dump('CLIENT CONNECTED');
            
            try {
                if ($encryption) {
                    var_dump('Negotiate TLS');
                    $socket->encrypt();
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
