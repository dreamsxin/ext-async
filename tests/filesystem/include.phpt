--TEST--
Filesystem supports include calls and will perform them sync.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--INI--
async.filesystem=1
--FILE--
<?php

namespace Concurrent;

Task::async(function () {
	var_dump('GO TASK');
    var_dump(require __DIR__ . '/assets/test.php');
    var_dump('DONE TASK');
});

var_dump('GO MAIN');
var_dump(require __DIR__ . '/assets/test.php');
var_dump('DONE MAIN');

--EXPECT--
string(7) "GO MAIN"
string(4) "WAIT"
string(7) "GO TASK"
string(4) "WAIT"
string(5) "READY"
string(11) "Hello World"
string(9) "DONE MAIN"
string(5) "READY"
string(11) "Hello World"
string(9) "DONE TASK"
