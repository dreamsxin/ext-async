--TEST--
Task awaiting arbitrary values.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

var_dump('A');

$defer = new Deferred();
$defer->resolve('B');

var_dump(Task::await($defer->awaitable()));

var_dump(Task::await(Task::async(function (string $x): string {
    return $x;
}, 'C')));

Task::await($t = Task::async(function () {
    return 'D';
}));

var_dump(Task::await($t));

?>
--EXPECT--
string(1) "A"
string(1) "B"
string(1) "C"
string(1) "D"
