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
$builder->configureStdout(ProcessBuilder::STDIO_INHERIT, ProcessBuilder::STDOUT);

$process = $builder->start(__DIR__ . '/504.inc');

var_dump('START');

(new \Concurrent\Timer(200))->awaitTimeout();

var_dump('SEND SIGNAL');

$process->signal(\Concurrent\SignalWatcher::SIGINT);

var_dump($process->awaitExit());
var_dump('FINISHED');

--EXPECT--
string(5) "START"
string(12) "AWAIT SIGNAL"
string(11) "SEND SIGNAL"
string(15) "SIGNAL RECEIVED"
int(4)
string(8) "FINISHED"
