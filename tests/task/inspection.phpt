--TEST--
Task exposes meta data.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$t = Task::async(function () { return [__FILE__, __LINE__]; });

var_dump(isset($t->status));
var_dump(empty($t->status));

var_dump(isset($t->foo));
var_dump(empty($t->foo));

var_dump($t->status);

list ($file, $line) = Task::await($t);

var_dump($t->status);
var_dump($file == $t->file);
var_dump($line == $t->line);

print_r(get_object_vars($t));
print_r($t);

?>
--EXPECTF--
bool(true)
bool(false)
bool(false)
bool(true)
string(7) "PENDING"
string(8) "RESOLVED"
bool(true)
bool(true)
Array
(
    [status] => RESOLVED
    [file] => %s
    [line] => %d
)
Concurrent\Task Object
(
    [status] => RESOLVED
    [file] => %s
    [line] => %d
)
