--TEST--
XP socket TCP cannot read after shutdown.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

require_once __DIR__ . '/assets/functions.php';

list ($a, $b) = socketpair();

var_dump(stream_socket_shutdown($a, STREAM_SHUT_RD));
var_dump(fread($a, 256));
var_dump(stream_get_meta_data($a)['eof']);

--EXPECT--
bool(true)
string(0) ""
bool(true)
