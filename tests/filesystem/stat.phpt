--TEST--
Filesystem can stat files and dirs.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--INI--
async.filesystem=1
--FILE--
<?php

namespace Concurrent;

$file = __DIR__ . '/assets/test.php';
$dir = __DIR__ . '/assets/dir';

$stat = stat($file);

var_dump(is_file($file));
var_dump(is_dir($file));
var_dump($stat['size'] == 124);

$stat = stat($dir);

var_dump(is_file($dir));
var_dump(is_dir($dir));

--EXPECT--
bool(true)
bool(false)
bool(true)
bool(false)
bool(true)
