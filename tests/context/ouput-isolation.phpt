--TEST--
Context can use isolated output buffers.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

error_reporting(-1);
ini_set('display_errors', (DIRECTORY_SEPARATOR == '\\') ? '0' : '1');

$t = Task::asyncWithContext(Context::current()->withIsolatedOutput(), function () {
    ob_start();
    var_dump('Go TASK');

    (new Timer(50))->awaitTimeout();

    var_dump('Done TASK');

    fwrite(STDERR, "INNER: {{ " . trim(preg_replace("'\s+'", ' ', ob_get_clean())) . " }}\n");
});

(new Timer(20))->awaitTimeout();

ob_start();

var_dump('START');
Task::await($t);
var_dump('END');

fwrite(STDERR, "OUTER: {{ " . trim(preg_replace("'\s+'", ' ', ob_get_clean())) . " }}\n");

?>
--EXPECT--
INNER: {{ string(7) "Go TASK" string(9) "Done TASK" }}
OUTER: {{ string(5) "START" string(3) "END" }}
