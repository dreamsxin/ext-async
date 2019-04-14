--TEST--
Process can inherit and change env.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Process;

$builder = ProcessBuilder::shell();
$builder = $builder->withStdoutInherited();

var_dump('START');

if (DIRECTORY_SEPARATOR == '\\') {
    $process = $builder->start('ping -n 2 localhost > NUL && echo DONE');
} else {
    $process = $builder->start('sleep 1 && echo DONE');
}

var_dump($process->__debugInfo()['running']);
var_dump($process->join());

--EXPECT--
string(5) "START"
bool(true)
DONE
int(0)
