<?php

namespace Concurrent\Network;

use Concurrent\Task;

function sslPair(): array
{
    $file = dirname(__DIR__) . '/examples/cert/localhost.';
    
    $encryption = new ServerEncryption();
    $encryption = $encryption->withDefaultCertificate($file . 'crt', $file . 'key', 'localhost');
    
    $server = TcpServer::listen('localhost', 0, $encryption);
    
    try {
        $t = Task::async(function () use ($server) {
            $encryption = new ClientEncryption();
            $encryption = $encryption->withAllowSelfSigned(true);
            $encryption = $encryption->withVerifyDepth(5);
            
            $socket = TcpSocket::connect($server->getHost(), $server->getPort(), $encryption);
            
            try {
                $socket->encrypt();
            } catch (\Throwable $e) {
                $socket->close();
                
                throw $e;
            }
            
            return $socket;
        });
        
        $socket = $server->accept();
        $socket->encrypt();
    } finally {
        $server->close();
    }
    
    return [
        $socket,
        Task::await($t)
    ];
}
