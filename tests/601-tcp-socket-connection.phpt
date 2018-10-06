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
    $host = $server->getHost();
    $port = $server->getPort();
    $peer = $server->getPeer();
    
    var_dump($host);
    var_dump($port > 0);
    
    var_dump($peer[0] == $host);
    var_dump($peer[1] == $port);
    
    Task::async(function () use ($host, $port) {
        $socket = TcpSocket::connect($host, $port);
        
        var_dump('CONNECTED');
        
        try {
            $local = $socket->getLocalPeer();
            
            var_dump($local[0] == $host);
            var_dump($local[1] > 0);
            
            $remote = $socket->getRemotePeer();
            
            var_dump($remote[0] == $host);
            var_dump($remote[1] == $port);
            
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
bool(true)
bool(true)
string(6) "LISTEN"
string(8) "ACCEPTED"
string(9) "CONNECTED"
bool(true)
bool(true)
bool(true)
bool(true)
string(5) "Hello"
string(6) "World!"
