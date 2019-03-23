--TEST--
Task exposes meta data.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
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

?>
--EXPECT--
bool(true)
bool(false)
bool(false)
bool(true)
string(7) "PENDING"
string(8) "RESOLVED"
bool(true)
bool(true)
