--TEST--
Task being garbage collected while supended due to continuation being destroyed.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
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

$b = null;

$scheduler = new TaskScheduler();

$t = $scheduler->task(function () use ($a, & $b) {
    try {
        var_dump(Task::await($a));
    } catch (\Throwable $e) {
        return $b = 321;
    }
});

$scheduler->run();

var_dump($a->continuation instanceof TaskContinuation);
var_dump(count($scheduler));

$a->continuation = null;

var_dump($b);

$t->continueWith(function ($e, $v) {
    var_dump($e, $v);
});

?>
--EXPECTF--
bool(true)
int(0)
int(321)
NULL
int(321)
