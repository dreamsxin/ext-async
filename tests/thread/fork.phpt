--TEST--
Thread will execute a script file in an isolated thread.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
if (\Concurrent\Thread::isAvailable()) echo 'Test requires ZTS';
?>
--FILE--
<?php

namespace Concurrent;

var_dump(Thread::isAvailable());
var_dump('START');

$thread = new Thread(__DIR__ . '/assets/fork.php');
$thread->join();

var_dump('DONE');

--EXPECT--
bool(true)
string(5) "START"
string(5) "READY"
string(9) "TERMINATE"
string(4) "DONE"
