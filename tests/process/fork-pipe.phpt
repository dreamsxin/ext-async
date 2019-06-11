--TEST--
Process can fork and transfer pipe to child process.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Process;

use Concurrent\Network\Pipe;

$builder = ProcessBuilder::fork(__DIR__ . '/assets/fork-pipe.php');

var_dump('START');

$process = $builder->start();
$ipc = $process->getIpc();

list ($a, $b) = Pipe::pair();

$a->export($ipc);
$a->close();

$b->write('Hello ');

var_dump($b->read());
var_dump($process->join());

--EXPECT--
string(5) "START"
string(12) "Hello World!"
int(0)
