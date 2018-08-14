--TEST--
Process can inherit and change env.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Process;

$builder = new ProcessBuilder(PHP_BINARY);
$builder->configureStdout(ProcessBuilder::STDIO_INHERIT, ProcessBuilder::STDOUT);

$builder->setEnv([
    'FOO' => 'BAR'
]);

var_dump('START');
var_dump($builder->execute(__DIR__ . '/506.inc'));
var_dump('FINISHED');

--EXPECT--
string(5) "START"
string(1) "Y"
string(3) "BAR"
int(0)
string(8) "FINISHED"
