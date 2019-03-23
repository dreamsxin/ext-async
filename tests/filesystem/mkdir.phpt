--TEST--
Filesystem can create and remove directories.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--INI--
async.filesystem=1
--FILE--
<?php

namespace Concurrent;

$dir = __DIR__ . '/assets/' . bin2hex(random_bytes(16));
$sub = $dir . '/' . bin2hex(random_bytes(8));

register_shutdown_function(function () use ($dir, $sub) {
	@rmdir($sub);
    @rmdir($dir);
});

var_dump('MKDIR');
var_dump(is_dir($dir));
var_dump(mkdir($dir));
var_dump(is_dir($dir));
var_dump(rmdir($dir));
var_dump(is_dir($dir));

var_dump('RECURSIVE');

$mask = umask(0);
var_dump(mkdir($sub, 0775, true));
umask($mask);

var_dump(is_dir($sub));

var_dump(sprintf('PERMS: %04o', fileperms($sub) & 0775));
var_dump(sprintf('PERMS: %04o', fileperms($dir) & 0775));

var_dump(@rmdir($dir));

--EXPECT--
string(5) "MKDIR"
bool(false)
bool(true)
bool(true)
bool(true)
bool(false)
string(9) "RECURSIVE"
bool(true)
bool(true)
string(11) "PERMS: 0775"
string(11) "PERMS: 0775"
bool(false)
