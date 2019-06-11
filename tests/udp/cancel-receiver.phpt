--TEST--
UDP cancel receive operation.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Context;
use Concurrent\Task;

$t = Task::asyncWithContext(Context::current()->withTimeout(500), function () {
    $socket = UdpSocket::bind('127.0.0.1', 0);
    
    var_dump('RECEIVING...');
    
    try {
        return $socket->receive();
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
string(12) "RECEIVING..."
string(32) "Concurrent\CancellationException"
string(32) "Concurrent\CancellationException"
