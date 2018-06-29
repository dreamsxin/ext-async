--TEST--
Task being garbage collected while supended.
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

$scheduler->task(function () use ($a, & $b) {
    try {
        var_dump(Task::await($a));
    } finally {
        $b = 321;
    }
});

$scheduler->run();

var_dump($a->continuation instanceof TaskContinuation);
var_dump(count($scheduler));

$a->continuation = null;

var_dump($b);

?>
--EXPECTF--
bool(true)
int(0)
int(321)
