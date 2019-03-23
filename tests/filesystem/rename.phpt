--TEST--
Filesystem can rename files.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--INI--
async.filesystem=1
--FILE--
<?php

namespace Concurrent;

$file = sys_get_temp_dir() . '/' . bin2hex(random_bytes(16)) . '.test';
$tmp = tempnam(sys_get_temp_dir(), '.test');

register_shutdown_function(function () use ($file, $tmp) {
    @unlink($file);
    @unlink($tmp);
});

file_put_contents($tmp, 'Hello World :)');

var_dump(is_file($file));
var_dump(rename($tmp, $file));
var_dump(is_file($tmp));
var_dump(is_file($file));
var_dump(file_get_contents($file));

var_dump(rename('/foo/bar/does/not/exist', sys_get_temp_dir() . '/foo'));

--EXPECT--
bool(false)
bool(true)
bool(false)
bool(true)
string(14) "Hello World :)"
bool(false)
