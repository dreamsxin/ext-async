--TEST--
Context with ouput isolation discards buffered content.
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

Context::current()->withIsolatedOutput()->run(function () {
    ob_start();
    
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
string(1) "A"
string(1) "B"
string(1) "C"
string(1) "Z"
