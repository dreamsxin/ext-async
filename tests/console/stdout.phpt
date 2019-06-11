--TEST--
Console can write to STDOUT.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--STDIN--
Hello World
--FILE--
<?php

namespace Concurrent;

use Concurrent\Stream\WritablePipe;

$stdout = WritablePipe::getStdout();

try {
    var_dump($stdout->isTerminal());

    $stdout->write("Hello\n");
    $stdout->write("World\n");
} finally {
    $stdout->close();
}

?>
--EXPECT--
bool(false)
Hello
World
