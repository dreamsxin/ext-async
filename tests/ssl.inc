<?php

namespace Concurrent\Network;

use Concurrent\Task;

function sslPair(): array
{
    $file = dirname(__DIR__) . '/examples/cert/localhost.';
    
    $tls = new TlsServerEncryption();
    $tls = $tls->withDefaultCertificate($file . 'crt', $file . 'key', 'localhost');
    
    $server = TcpServer::listen('localhost', 0, $tls);
    
    try {
        $t = Task::async(function () use ($server) {
            $tls = new TlsClientEncryption();
            $tls = $tls->withAllowSelfSigned(true);
            $tls = $tls->withVerifyDepth(5);
            
            $socket = TcpSocket::connect($server->getAddress(), $server->getPort(), $tls);
            
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
