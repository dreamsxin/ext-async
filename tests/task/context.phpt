--TEST--
Task scheduler with context.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

TaskScheduler::runWithContext(Context::current(), function () {
    Task::async(function () {
        (new Timer(10))->awaitTimeout();
        var_dump('DONE');
    });
}, function (array $tasks) {
    var_dump(count($tasks));
});

try {
    TaskScheduler::runWithContext(Context::current(), function () {
        throw new \Error('FOO');
    });
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

TaskScheduler::runWithContext(Context::current(), function () {
    echo 'END';
    exit();
});

?>
--EXPECT--
int(1)
string(4) "DONE"
string(3) "FOO"
END
