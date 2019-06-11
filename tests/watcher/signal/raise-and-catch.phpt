--TEST--
Signal can raise and catch signals.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

Task::async(function () {
    $timer = new Timer(100);
    $timer->awaitTimeout();
    
    var_dump('TRIGGER');
    Signal::raise(Signal::SIGUSR1);
    
    $timer->awaitTimeout();
    
    var_dump('TRIGGER');
    Signal::raise(Signal::SIGUSR1);
});

$signal = new Signal(Signal::SIGUSR1);

var_dump('AWAIT SIGNAL');
$signal->awaitSignal();

var_dump('AWAIT SIGNAL');
$signal->awaitSignal();

var_dump('CLOSE');
$signal->close();

try {
    $signal->awaitSignal();
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

--EXPECT--
string(12) "AWAIT SIGNAL"
string(7) "TRIGGER"
string(12) "AWAIT SIGNAL"
string(7) "TRIGGER"
string(5) "CLOSE"
string(22) "Signal has been closed"
