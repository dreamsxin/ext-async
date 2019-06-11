--TEST--
Deferred basic API.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$a = TaskScheduler::run(function () {
    $line = __LINE__; $x = Deferred::value(321);
    
    var_dump($x->status == 'RESOLVED');
    var_dump($x->file == __FILE__);
    var_dump($x->line == $line);

	return Task::await(Deferred::value(321));
});

var_dump($a);

try {
    var_dump(TaskScheduler::run(function () {
        $e = Deferred::error(new \Error('Fail!'));
    
        var_dump('X');
    
        return Task::await($e);
    }));
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

$a = TaskScheduler::run(function () {
    $defer = new Deferred();
    
    Task::async(function () use ($defer) {
        var_dump('B');
        
        $x = $defer->awaitable();
        $defer->resolve(777);
        
        print_r(get_object_vars($defer));
        print_r(get_object_vars($x));
        
        var_dump($defer->status == $x->status);
	    var_dump($defer->file == $x->file);
	    var_dump($defer->line == $x->line);
    });
    
    var_dump('A');
    
    $x = $defer->awaitable();
    
    var_dump(isset($defer->status));
    var_dump(isset($x->status));
    var_dump(isset($defer->file));
    var_dump(isset($x->file));
    var_dump(isset($defer->line));
    var_dump(isset($x->line));
    
    print_r($defer);
    print_r($x);
    
    var_dump($defer->status == $x->status);
    var_dump($defer->file == $x->file);
    var_dump($defer->line == $x->line);
        
    return Task::await($x);
});

var_dump($a);

var_dump(TaskScheduler::run(function () {
    $defer = new Deferred();
    $defer->resolve();
    
    return Task::await($defer->awaitable());
}));

?>
--EXPECTF--
bool(true)
bool(true)
bool(true)
int(321)
string(1) "X"
string(5) "Fail!"
string(1) "A"
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
Concurrent\Deferred Object
(
    [status] => PENDING
    [file] => %s
    [line] => %d
)
Concurrent\DeferredAwaitable Object
(
    [status] => PENDING
    [file] => %s
    [line] => %d
)
bool(true)
bool(true)
bool(true)
string(1) "B"
Array
(
    [status] => RESOLVED
    [file] => %s
    [line] => %d
)
Array
(
    [status] => RESOLVED
    [file] => %s
    [line] => %d
)
bool(true)
bool(true)
bool(true)
int(777)
NULL
