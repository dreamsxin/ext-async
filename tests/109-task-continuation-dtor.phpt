--TEST--
Task will dispose references to callbacks when destroyed.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

class Continuation {
    public function __destruct() {
        var_dump('DTOR');
    }

    public function __invoke() { }
}

$a = new class() implements Awaitable {
    public $cont;
    
    public function continueWith($c) {
        $this->cont = $c;
    }
};

$scheduler = new TaskScheduler();

$t = $scheduler->task(function () use ($a) {
   Task::await($a);
});

$t->continueWith(new Continuation());

var_dump('START');

$scheduler->run();

$a->cont = null;

var_dump('END');

?>
--EXPECTF--
string(5) "START"
string(4) "DTOR"
string(3) "END"
