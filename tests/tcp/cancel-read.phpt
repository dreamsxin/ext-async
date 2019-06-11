--TEST--
TCP spcket cancel read.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Context;
use Concurrent\Task;

$t = Task::asyncWithContext(Context::current()->withTimeout(500), function () {
    list ($a, $b) = TcpSocket::pair();
    
    var_dump('READING...');
    
    try {
        return $b->read();
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
string(10) "READING..."
string(32) "Concurrent\CancellationException"
string(32) "Concurrent\CancellationException"
