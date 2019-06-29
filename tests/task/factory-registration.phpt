--TEST--
Task scheduler manages factories.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

class A {
    public function __invoke() { }
}

$f1 = TaskScheduler::register(TaskScheduler::class, new A());
$f2 = TaskScheduler::register(TaskScheduler::class, NULL);

var_dump($f1);
var_dump(get_class($f2));


?>
--EXPECT--
NULL
string(12) "Concurrent\A"
