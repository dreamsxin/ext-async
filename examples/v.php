<?php

namespace Concurrent\Network;

$server = TcpServer::listen('localhost', 8080);

try {
    for ($i = 0; $i < 3; $i++) {
        $socket = $server->accept();
        
        \Concurrent\Task::async(function () use ($socket) {
            var_dump('CLIENT CONNECTED');
            
            $buffer = '';
            
            try {
                while (null !== ($chunk = $socket->read())) {
                    $buffer .= $chunk;
                }
                
                var_dump($buffer);
                
                $socket->write('RECEIVED: ' . \strtoupper($buffer));
            } finally {
                $socket->close();
                
                var_dump('CLIENT DISCONNECTED');
            }
        });
    }
} finally {
    $server->close();
}
