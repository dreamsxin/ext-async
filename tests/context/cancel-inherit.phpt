--TEST--
Context cancellation inherits parent error during creation.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$h1 = null;
$c1 = Context::current()->withCancel($h1);

$h1(new \Error('FOO!'));

$h2 = null;
$c2 = $c1->withCancel($h2);

try {
    $c2->withIsolatedOutput()->run(function () {
        (new Timer(10))->awaitTimeout();
    });
} catch (CancellationException $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}

?>
--EXPECT--
string(26) "Context has been cancelled"
string(4) "FOO!"
