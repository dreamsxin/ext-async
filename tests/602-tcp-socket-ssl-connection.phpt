--TEST--
TCP socket SSL connection.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Task;

$file = dirname(__DIR__) . '/examples/cert/localhost.';

$tls = new TlsServerEncryption();
$tls = $tls->withDefaultCertificate($file . 'crt', $file . 'key', 'localhost');

$server = TcpServer::listen('127.0.0.1', 0, $tls);

try {
    $host = $server->getAddress();
    $port = $server->getPort();
    
    Task::async(function () use ($host, $port) {
        $tls = new TlsClientEncryption();
        $tls = $tls->withPeerName('localhost');
        $tls = $tls->withAllowSelfSigned(true);
        
        $socket = TcpSocket::connect($host, $port, $tls);
        
        var_dump('CONNECTED');
        
        try {
            var_dump('START CLIENT HANDSHAKE');
            $socket->encrypt();
            var_dump('CLIENT HANDSHAKE DONE');
        
            var_dump($socket->read());
            $socket->write('World!');
        } catch (\Throwable $e) {
            echo $e, "\n\n";
        } finally {
            $socket->close();
        }
    });
    
    var_dump('LISTEN');
    $socket = $server->accept();
    var_dump('ACCEPTED');
    
    try {
        var_dump('START SERVER HANDSHAKE');
        $socket->encrypt();
        var_dump('SERVER HANDSHAKE DONE');
        
        $socket->write('Hello');
        
        while (null !== ($chunk = $socket->read())) {
            var_dump($chunk);
        }
    } finally {
        $socket->close();
    }
} finally {
    $server->close();
}

--EXPECT--
string(6) "LISTEN"
string(8) "ACCEPTED"
string(22) "START SERVER HANDSHAKE"
string(9) "CONNECTED"
string(22) "START CLIENT HANDSHAKE"
string(21) "SERVER HANDSHAKE DONE"
string(21) "CLIENT HANDSHAKE DONE"
string(5) "Hello"
string(6) "World!"
