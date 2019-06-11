--TEST--
Process can fork TCP server and listen in multiple processes.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Process;

use Concurrent\Network\TcpServer;
use Concurrent\Network\TcpSocket;

$server = TcpServer::bind('127.0.0.1', 0);
$port = $server->getPort();

$builder = ProcessBuilder::fork(__DIR__ . '/assets/fork-server.php');
$workers = [];

var_dump('START');

for ($i = 0; $i < 2; $i++) {
    $process = $builder->start();
    $ipc = $process->getIpc();

    $server->export($ipc);

    var_dump($ipc->read());
    
    $workers[] = $process;
}

$server->close();

for ($i = 0; $i < 2; $i++) {
    $socket = TcpSocket::connect('127.0.0.1', $port);

    try {
        $socket->write('Hello ');
    
        var_dump($socket->read());
    } finally {
        $socket->close();
    }
}

foreach ($workers as $process) {
    var_dump($process->join());
}

--EXPECT--
string(5) "START"
NULL
NULL
string(12) "Hello World!"
string(12) "Hello World!"
int(0)
int(0)
