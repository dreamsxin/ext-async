--TEST--
XP socket TCP enforces read timeout.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

require_once __DIR__ . '/assets/functions.php';

list ($a, $b) = socketpair();

var_dump(stream_set_timeout($a, 0, 20000));
var_dump(stream_get_meta_data($a)['timed_out']);

var_dump(fread($a, 8192));
var_dump(stream_get_meta_data($a)['timed_out']);

--EXPECT--
bool(true)
bool(false)
string(0) ""
bool(true)
