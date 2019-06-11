--TEST--
Thread can deal with worker not connecting to IPC pipe.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

var_dump('START');

$thread = new Thread(__DIR__ . '/assets/ipc-no-conn.php');
$ipc = $thread->getIpc();

try {
    var_dump($ipc->read());
} finally {
    $ipc->close();
}

$thread->join();

var_dump('DONE');

--EXPECT--
string(5) "START"
string(10) "GO AWAY..."
NULL
string(4) "DONE"
