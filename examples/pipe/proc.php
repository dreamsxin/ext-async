<?php

namespace Concurrent;

use Concurrent\Network\TcpSocket;
use Concurrent\Process\ProcessBuilder;

error_reporting(-1);

$process = ProcessBuilder::fork(__DIR__ . '/worker.php')->start();
$ipc = $process->getIpc();

try {
    $tcp = TcpSocket::connect('httpbin.org', 80);
    $tcp->writeAsync("GET /json HTTP/1.0\r\nHost: httpbin.org\r\nConnection: close\r\n\r\n");
    
    var_dump('SEND HANDLE');
    $tcp->export($ipc);
    $tcp->close();    
} finally {
    $ipc->close();
}

var_dump('AWAIT DATA...');
printf("\nEXIT CODE: %u\n", $process->join());
