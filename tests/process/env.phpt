--TEST--
Process can inherit and change env.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Process;

$builder = new ProcessBuilder(PHP_BINARY);
$builder = $builder->withStdoutPipe();

$builder = $builder->withEnv([
    'FOO' => 'BAR'
], true);

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
