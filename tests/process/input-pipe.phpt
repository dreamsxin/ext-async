--TEST--
Process provides STDIN as writable pipe.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Process;

$builder = new ProcessBuilder(PHP_BINARY);
$builder->configureStdin(ProcessBuilder::STDIO_PIPE);
$builder->configureStdout(ProcessBuilder::STDIO_INHERIT, ProcessBuilder::STDOUT);

$process = $builder->start(__DIR__ . '/assets/stdin-dump.php');

var_dump('START');

$stdin = $process->getStdin();

var_dump($stdin instanceof \Concurrent\Stream\WritableStream);

try {
    $stdin->write("Hello\n");
    
    (new \Concurrent\Timer(100))->awaitTimeout();
    
    $stdin->write('World :)');
} finally {
    $stdin->close();
}

var_dump($process->join());
var_dump('FINISHED');

--EXPECT--
string(5) "START"
bool(true)
string(5) "Hello"
string(8) "World :)"
string(12) "STDIN CLOSED"
int(0)
string(8) "FINISHED"
