--TEST--
Process can fork and transfer TCP server to child process.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Process;

use Concurrent\Network\TcpServer;
use Concurrent\Network\TcpSocket;

$server = TcpServer::bind('127.0.0.1', 0);
$port = $server->getPort();

$builder = ProcessBuilder::fork(__DIR__ . '/assets/fork-server.php');

var_dump('START');

$process = $builder->start();
$ipc = $process->getIpc();

$server->export($ipc);
$server->close();

var_dump($ipc->read());

$socket = TcpSocket::connect('127.0.0.1', $port);

try {
    $socket->write('Hello ');
    
    var_dump($socket->read());
} finally {
    $socket->close();
}

var_dump($process->join());

--EXPECT--
string(5) "START"
NULL
string(12) "Hello World!"
int(0)
