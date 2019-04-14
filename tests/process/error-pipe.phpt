--TEST--
Process provides STDERR as readable pipe.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Process;

$builder = new ProcessBuilder(PHP_BINARY);
$builder = $builder->withoutStdout();
$builder = $builder->withStderrPipe();

$process = $builder->start(__DIR__ . '/assets/stderr-output.php');

var_dump('START');

$stderr = $process->getStderr();

var_dump($stderr instanceof \Concurrent\Stream\ReadableStream);

try {
    while (null !== ($chunk = $stderr->read())) {
        var_dump($chunk);
    }
} finally {
    $stderr->close();
}

var_dump($process->join());
var_dump('FINISHED');

--EXPECT--
string(5) "START"
bool(true)
string(5) "Hello"
string(8) "World :)"
int(1)
string(8) "FINISHED"
