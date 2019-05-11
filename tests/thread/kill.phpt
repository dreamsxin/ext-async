--TEST--
Thread can be killed using interrupt.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
if (\Concurrent\Thread::isAvailable()) echo 'Test requires ZTS';
?>
--FILE--
<?php

namespace Concurrent;

var_dump('START');

$thread = new Thread(__DIR__ . '/assets/kill.php');

(new Timer(200))->awaitTimeout();

$thread->kill();
$thread->join();

var_dump('DONE');

--EXPECT--
string(5) "START"
string(3) "RUN"
string(4) "DONE"
