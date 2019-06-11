--TEST--
Task scheduling using the static task API.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$t = Task::async(function (string $title) {
    var_dump($title);
}, 'A');

var_dump($t instanceof Task);

Task::async(function () {
    var_dump('B');
});

$work = function () {
    var_dump('C');
};

Task::async($work);

?>
--EXPECT--
bool(true)
string(1) "A"
string(1) "B"
string(1) "C"
