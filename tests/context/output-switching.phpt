--TEST--
Context can switch to output isolation.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

error_reporting(-1);
ini_set('display_errors', (DIRECTORY_SEPARATOR == '\\') ? '0' : '1');

ob_start();

var_dump('A');

$context = Context::current()->withIsolatedOutput();

Context::current()->withIsolatedOutput()->run(function () {
    var_dump('X');
    (new Timer(100))->awaitTimeout();
    var_dump('Y');
});

var_dump('B');

var_dump('C');
echo ob_get_clean();
var_dump('Z');

?>
--EXPECT--
string(1) "X"
string(1) "Y"
string(1) "A"
string(1) "B"
string(1) "C"
string(1) "Z"
