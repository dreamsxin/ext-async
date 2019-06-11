--TEST--
Process provides access code after execution.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Process;

$builder = new ProcessBuilder(PHP_BINARY);
$builder = $builder->withCwd(__DIR__);
$builder = $builder->withEnv([], false);

$builder = $builder->withoutStdin();
$builder = $builder->withStdoutInherited();

var_dump('START');
var_dump($builder->execute('assets/running.php'));
var_dump('FINISHED');

--EXPECT--
string(5) "START"
string(7) "RUNNING"
int(7)
string(8) "FINISHED"
