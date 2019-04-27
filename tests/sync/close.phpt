--TEST--
Condition is unusable after close.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

use Concurrent\Sync\Condition;

$cond = new Condition();

Task::async(function () use ($cond) {
    (new Timer(50))->awaitTimeout();
    
    $cond->close(new \Exception('FOO'));
});

try {
    $cond->wait();
} catch (\Throwable $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}

$cond->close();

try {
    $cond->signal();
} catch (\Throwable $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}

try {
    $cond->broadcast();
} catch (\Throwable $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}

--EXPECT--
string(25) "Condition has been closed"
string(3) "FOO"
string(25) "Condition has been closed"
string(3) "FOO"
string(25) "Condition has been closed"
string(3) "FOO"
