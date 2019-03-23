--TEST--
TCP cancel queued write operation.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Context;
use Concurrent\Task;
use Concurrent\Timer;

list ($a, $b) = TcpSocket::pair();

$t1 = Task::async(function () use ($a) {
    $a->write(str_repeat('A', 1024 * 1024 * 8));
});

$t2 = Task::asyncWithContext(Context::current()->withTimeout(50), function () use ($a) {
    $a->write('FOO');
});

(new Timer(10))->awaitTimeout();

var_dump('START');

try {
    Task::await($t2);
} catch (\Throwable $e) {
    var_dump($e->getMessage());
} finally {
    var_dump('CLOSE!');
    
    $b->close();
}

var_dump('DONE');

--EXPECT--
string(5) "START"
string(21) "Context has timed out"
string(6) "CLOSE!"
string(4) "DONE"
