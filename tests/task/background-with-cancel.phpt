--TEST--
Task can run in background with cancellation handler.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$cancel = null;

$t = Task::asyncWithContext(Context::background()->withCancel($cancel), function () {
    return 321;
});

var_dump(Task::await($t));

$cancel = null;

$t = Task::asyncWithContext(Context::background()->withCancel($cancel), function () {
    (new Timer(500))->awaitTimeout();

    return 123;
});

(new Timer(100))->awaitTimeout();

$cancel(new \Error('GIVE UP!'));

try {
    var_dump(Task::await($t));
} catch (CancellationException $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}

--EXPECT--
int(321)
string(26) "Context has been cancelled"
string(8) "GIVE UP!"
