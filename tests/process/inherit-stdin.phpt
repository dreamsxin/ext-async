--TEST--
Process can inherit STDIN from parent.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--STDIN--
Hello World
This is some dummy data
--FILE--
<?php

namespace Concurrent\Process;

$builder = new ProcessBuilder(PHP_BINARY);
$builder = $builder->withStdinInherited();
$builder = $builder->withStdoutInherited();

var_dump('SPAWN');

$builder->execute(__DIR__ . '/assets/stdin-dump.php');

--EXPECT--
string(5) "SPAWN"
string(11) "Hello World"
string(23) "This is some dummy data"
string(12) "STDIN CLOSED"
