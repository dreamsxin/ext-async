--TEST--
TCP socket connection.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Task;

$server = TcpServer::listen('127.0.0.1', 0);

try {
    $host = $server->getAddress();
    $port = $server->getPort();
    
    var_dump($host);
    var_dump($port > 0);
    
    Task::async(function () use ($host, $port) {
        $socket = TcpSocket::connect($host, $port);
        
        var_dump('CONNECTED');
        
        try {
            var_dump($socket->getAddress() == $host);
            var_dump($socket->getPort() > 0);
            
            var_dump($socket->getRemoteAddress() == $host);
            var_dump($socket->getRemotePort() == $port);
            
            var_dump($socket->read());
            $socket->write('World!');
        } finally {
            $socket->close();
        }
    });
    
    var_dump('LISTEN');
    $socket = $server->accept();
    var_dump('ACCEPTED');
    
    try {
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
string(9) "127.0.0.1"
bool(true)
string(6) "LISTEN"
string(9) "CONNECTED"
bool(true)
bool(true)
bool(true)
bool(true)
string(8) "ACCEPTED"
string(5) "Hello"
string(6) "World!"
