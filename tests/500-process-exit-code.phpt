--TEST--
Process provides access code after execution.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Process;

$builder = new ProcessBuilder(PHP_BINARY);
$builder->setDirectory(__DIR__);
$builder->inheritEnv(false);

$builder->configureStdin(ProcessBuilder::STDIO_IGNORE);
$builder->configureStdout(ProcessBuilder::STDIO_INHERIT, ProcessBuilder::STDOUT);

var_dump('START');
var_dump($builder->execute('500.inc'));
var_dump('FINISHED');

--EXPECT--
string(5) "START"
string(7) "RUNNING"
int(7)
string(8) "FINISHED"
