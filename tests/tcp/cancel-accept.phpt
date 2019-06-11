--TEST--
TCP server cancel accept.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Context;
use Concurrent\Task;

$t = Task::asyncWithContext(Context::current()->withTimeout(500), function () {
    $server = TcpServer::listen('127.0.0.1', 0);
    
    var_dump('LISTENING...');
    
    try {
        return $server->accept();
    } catch (\Throwable $e) {
        var_dump(get_class($e));
        
        throw $e;
    }
});

try {
    Task::await($t);
} catch (\Throwable $e) {
    var_dump(get_class($e));
}

--EXPECT--
string(12) "LISTENING..."
string(32) "Concurrent\CancellationException"
string(32) "Concurrent\CancellationException"
