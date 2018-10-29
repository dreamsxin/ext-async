--TEST--
TCP spcket cancel read.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
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
        var_dump($e->getMessage());
        
        throw $e;
    }
});

try {
    Task::await($t);
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

--EXPECT--
string(10) "READING..."
string(17) "Context timed out"
string(17) "Context timed out"
