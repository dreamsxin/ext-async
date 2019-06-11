--TEST--
Filesystem provides read streams.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--INI--
async.filesystem=1
--FILE--
<?php

namespace Concurrent;

$fp = fopen(__DIR__ . '/assets/dir/sub/hello.txt', 'rb');
$meta = stream_get_meta_data($fp);

var_dump($meta['wrapper_type'], $meta['stream_type']);

var_dump(feof($fp));
var_dump(stream_get_contents($fp));
var_dump(feof($fp));

rewind($fp);

var_dump(feof($fp));
var_dump(stream_get_contents($fp));
var_dump(feof($fp));

var_dump(ftell($fp));
var_dump(fseek($fp, -10, SEEK_END));
var_dump(fseek($fp, -10, SEEK_CUR));
var_dump(ftell($fp));

var_dump(feof($fp));
var_dump(stream_get_contents($fp));
var_dump(feof($fp));

fclose($fp);

var_dump(is_resource($fp));

--EXPECT--
string(15) "plainfile/async"
string(11) "STDIO/async"
bool(false)
string(5) "WORLD"
bool(true)
bool(false)
string(5) "WORLD"
bool(true)
int(5)
int(-1)
int(-1)
int(5)
bool(true)
string(0) ""
bool(true)
bool(false)
