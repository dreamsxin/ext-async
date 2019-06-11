--TEST--
Process can inherit and change env.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Process;

$builder = new ProcessBuilder(PHP_BINARY);
$builder = $builder->withStdoutInherited();

$builder = $builder->withEnv([
    'FOO' => 'BAR'
], true);

var_dump('START');
var_dump($builder->execute(__DIR__ . '/assets/env2.php'));
var_dump('FINISHED');

--EXPECT--
string(5) "START"
string(1) "Y"
string(3) "BAR"
int(0)
string(8) "FINISHED"
