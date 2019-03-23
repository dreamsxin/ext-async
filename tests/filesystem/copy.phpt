--TEST--
Filesystem can copy files.
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

register_shutdown_function(function () use ($file) {
    @unlink($file);
});

var_dump(is_file($file));
var_dump(copy(__DIR__ . '/assets/dir/sub/hello.txt', $file));
var_dump(is_file($file));
var_dump(file_get_contents($file));

--EXPECT--
bool(false)
bool(true)
bool(true)
string(5) "WORLD"
