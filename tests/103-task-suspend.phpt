--TEST--
Task being suspended and resumed.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'skip';
?>
--FILE--
<?php

namespace Concurrent;

$a = new class() implements Awaitable {
   public $continuation;
   
   public function continueWith(callable $continuation) {
       $this->continuation = $continuation;
   }
};

$scheduler = new TaskScheduler();

$t = $scheduler->task(function () use ($a) {
    var_dump(Task::await($a));
});

$scheduler->run();

var_dump($a->continuation instanceof Taskcontinuation);
var_dump(count($scheduler));

($a->continuation)(null, 'X');

var_dump(count($scheduler));

$scheduler->run();

var_dump(count($scheduler));

?>
--EXPECTF--
bool(true)
int(0)
int(1)
string(1) "X"
int(0)
