--TEST--
Process can be signalled.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Process;

$builder = new ProcessBuilder(PHP_BINARY);
$builder = $builder->withStdoutInherited();

$process = $builder->start(__DIR__ . '/assets/signal.php');

var_dump('START');

(new \Concurrent\Timer(200))->awaitTimeout();

var_dump('SEND SIGNAL');

$process->signal(\Concurrent\SignalWatcher::SIGINT);

var_dump($process->join());
var_dump('FINISHED');

--EXPECT--
string(5) "START"
string(12) "AWAIT SIGNAL"
string(11) "SEND SIGNAL"
string(15) "SIGNAL RECEIVED"
int(4)
string(8) "FINISHED"
