--TEST--
Thread can deal with worker not connecting to IPC pipe.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
if (\Concurrent\Thread::isAvailable()) echo 'Test requires ZTS';
?>
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
