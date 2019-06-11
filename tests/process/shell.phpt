--TEST--
Process can inherit and change env.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
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


print_r($process);

var_dump($process->join());

--EXPECTF--
string(5) "START"
Concurrent\Process\Process Object
(
    [pid] => %d
    [exit_code] => -1
    [running] => 1
)
DONE
int(0)
