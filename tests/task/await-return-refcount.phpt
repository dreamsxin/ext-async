--TEST--
Task properly deals with refcount in return / await situation.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

var_dump('START');

$t = Task::async(function ($foo) {
	return Deferred::value($foo);
}, new class () {
    public function __destruct() {
        var_dump('DISPOSE');
    }
});

(new Timer(10))->awaitTimeout();

$v = Task::await($t);
$t = null;

(new Timer(10))->awaitTimeout();

var_dump(is_object($v));

var_dump('UNREF');
$v = null;

var_dump('DONE');

--EXPECT--
string(5) "START"
bool(true)
string(5) "UNREF"
string(7) "DISPOSE"
string(4) "DONE"
