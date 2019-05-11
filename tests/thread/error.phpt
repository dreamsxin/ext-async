--TEST--
Thread will forward error details to master.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
if (\Concurrent\Thread::isAvailable()) echo 'Test requires ZTS';
?>
--FILE--
<?php

namespace Concurrent;

$thread = new Thread(__DIR__ . '/assets/error.php');

var_dump($thread->join());

--EXPECT--
string(8) "GO THROW"
int(1)
