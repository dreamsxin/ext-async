--TEST--
Console can read from STDIN.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--STDIN--
Hello World
--FILE--
<?php

namespace Concurrent;

use Concurrent\Stream\ReadablePipe;

$stdin = ReadablePipe::getStdin();

try {
    var_dump($stdin->isTerminal());
    var_dump(trim($stdin->read()));
} finally {
    $stdin->close();
}

$stdin = ReadablePipe::getStdin();

var_dump($stdin->read());

?>
--EXPECT--
bool(false)
string(11) "Hello World"
NULL
