--TEST--
Task being suspended and resumed.
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

$scheduler = new TaskScheduler();

$scheduler->task(function () {
    var_dump('A');
    
    Task::async(function () {
        var_dump('C');
    });
});

$scheduler->task(function () use ($a) {
    var_dump(Task::await($a));
});

$scheduler->task(function () {
    var_dump('B');
    
    Task::async(function () {
       var_dump('D');
   });
});

$scheduler->run();

var_dump($a->continuation instanceof TaskContinuation);
var_dump(count($scheduler));

($a->continuation)(null, 'X');

var_dump(count($scheduler));

$scheduler->run();

var_dump(count($scheduler));

?>
--EXPECTF--
string(1) "A"
string(1) "B"
string(1) "C"
string(1) "D"
bool(true)
int(0)
int(1)
string(1) "X"
int(0)
