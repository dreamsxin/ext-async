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
$builder->configureStdout(ProcessBuilder::STDIO_PIPE);

$builder->setEnv([
    'FOO' => 'BAR'
]);

var_dump('START');

$process = $builder->start(__DIR__ . '/assets/env.php');

$stdout = $process->getStdout();
$buffer = [];

try {
    while (null !== ($chunk = $stdout->read())) {
        $buffer[] = $chunk;
    }
} finally {
    $stdout->close();
}

var_dump($buffer[0] == getenv('PATH'));
var_dump($buffer[1] == 'BAR');

var_dump($process->join());
var_dump('FINISHED');

--EXPECT--
string(5) "START"
bool(true)
bool(true)
int(0)
string(8) "FINISHED"
