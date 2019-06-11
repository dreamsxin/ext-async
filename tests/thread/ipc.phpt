--TEST--
Thread can utilize IPC pipes.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

var_dump('START');

$thread = new Thread(__DIR__ . '/assets/ipc.php');

$ipc = $thread->getIpc();
$ipc = $thread->getIpc();

try {
    $ipc->write('Hello');
    
    while (null !== ($chunk = $ipc->read())) {
        var_dump($chunk);
    }
} finally {
    $ipc->close();
}

$thread->join();

var_dump('DONE');

--EXPECT--
string(5) "START"
bool(true)
string(5) "Hello"
string(5) "World"
string(4) "EXIT"
string(4) "DONE"
