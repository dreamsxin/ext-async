<?php

namespace Concurrent\Process;

use Concurrent\Network\TcpServer;
use Concurrent\Task;
use Concurrent\Timer;

ini_set('html_errors', '0');
ini_set('xdebug.overload_var_dump', '0');

$ipc = Process::forked();

$server = TcpServer::import($ipc);
$server->setOption(TcpServer::SIMULTANEOUS_ACCEPTS, false);

Task::async(function () use ($ipc) {
    $ipc->close();
});

try {
    $socket = $server->accept();
} finally {
    $server->close();
}

try {
    $socket->write($socket->read() . 'World!');
} finally {
    $socket->close();
}
