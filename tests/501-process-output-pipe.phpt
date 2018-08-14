--TEST--
Process provides STDOUT as readable pipe.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Process;

$builder = new ProcessBuilder(PHP_BINARY);
$builder->configureStdout(ProcessBuilder::STDIO_PIPE);

$process = $builder->start(__DIR__ . '/500.inc');

var_dump($process->isRunning(), $process->getPid() > 0, 'START');

$stdout = $process->getStdout();

var_dump($stdout instanceof \Concurrent\Stream\ReadableStream);

try {
    while (null !== ($chunk = $stdout->read())) {
        echo $chunk;
    }
} finally {
    $stdout->close();
}

var_dump($process->awaitExit());
var_dump($process->awaitExit());

var_dump($process->isRunning(), 'FINISHED');

--EXPECT--
bool(true)
bool(true)
string(5) "START"
bool(true)
string(7) "RUNNING"
int(7)
int(7)
bool(false)
string(8) "FINISHED"
