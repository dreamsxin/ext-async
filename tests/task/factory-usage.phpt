--TEST--
Task scheduler uses factories to load manage objects.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

try {
    TaskScheduler::get(\stdClass::class);
} catch (\Error $e) {
    var_dump($e->getMessage());
}

TaskScheduler::register(TaskScheduler::class, function () {
    throw new \Error('FOO!');
});

try {
    TaskScheduler::get(TaskScheduler::class);
} catch (\Error $e) {
    var_dump($e->getMessage());
}

TaskScheduler::register(TaskScheduler::class, function () {
    return 123;
});

try {
    TaskScheduler::get(TaskScheduler::class);
} catch (\Error $e) {
    var_dump($e->getMessage());
}

TaskScheduler::register(TaskScheduler::class, function () {
    return new Deferred();
});

try {
    TaskScheduler::get(TaskScheduler::class);
} catch (\Error $e) {
    var_dump($e->getMessage());
}

TaskScheduler::register(TaskScheduler::class, function () {
    var_dump('END');
    exit();
});

Task::async(function () {
    TaskScheduler::get(TaskScheduler::class);
});

?>
--EXPECT--
string(39) "No factory registered for type stdClass"
string(4) "FOO!"
string(59) "Factory must return an instance of Concurrent\TaskScheduler"
string(59) "Factory must return an instance of Concurrent\TaskScheduler"
string(3) "END"
