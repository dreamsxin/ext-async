--TEST--
Process builder will filter and validate env vars.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Process;

$builder = new ProcessBuilder(PHP_BINARY);

try {
    $builder->withEnv(['foo', 'bar']);
} catch (\Throwable $e) {
	var_dump($e->getMessage());
}

$builder = $builder->withEnv(['FOO' => 123]);
$builder = $builder->withStdoutInherited();

var_dump($builder->execute(__DIR__ . '/assets/env-foo.php'));

--EXPECT--
string(34) "Cannot use index 0 as env var name"
string(3) "123"
int(0)
