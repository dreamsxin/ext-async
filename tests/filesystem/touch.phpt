--TEST--
Filesystem can touch files
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--INI--
async.filesystem=1
--FILE--
<?php

namespace Concurrent;

$file = 'async-file://' . sys_get_temp_dir() . '/' . bin2hex(random_bytes(16)) . '.test';
$time = time() - 300;
$time2 = time() - 50;

var_dump('EXISTS: ' . (int)is_file($file));
var_dump('TOUCH: ' . (int)touch($file, $time, $time));
var_dump('EXISTS: ' . (int)is_file($file));

var_dump('MTIME: ' . (int)(filemtime($file) == $time));
var_dump('ATIME: ' . (int)(fileatime($file) == $time));

var_dump('TOUCH: ' . (int)touch($file, $time2));
clearstatcache();

var_dump('MTIME: ' . (int)(filemtime($file) == $time2));
var_dump('ATIME: ' . (int)(fileatime($file) == $time2));

$mask = umask(0);
var_dump('CHMOD: ' . (int)chmod($file, 0775));
umask($mask);

var_dump(sprintf('PERMS: %04o', (fileperms($file) & 0775)));

register_shutdown_function(function () use ($file) {
	@unlink($file);
});

--EXPECT--
string(9) "EXISTS: 0"
string(8) "TOUCH: 1"
string(9) "EXISTS: 1"
string(8) "MTIME: 1"
string(8) "ATIME: 1"
string(8) "TOUCH: 1"
string(8) "MTIME: 1"
string(8) "ATIME: 1"
string(8) "CHMOD: 1"
string(11) "PERMS: 0775"