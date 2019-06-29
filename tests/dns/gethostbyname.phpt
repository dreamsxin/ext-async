--TEST--
DNS gethostbyname.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--INI--
async.dns=1
--FILE--
<?php

namespace Concurrent;

var_dump(gethostbyname(gethostname()));
var_dump(gethostbyname('localhost'));
var_dump(gethostbyname('1.2.3.4'));

$a = str_repeat('A', 2048);
var_dump(gethostbyname($a) == $a);

--EXPECTF--
string(%d) "%s.%s.%s.%s"
string(9) "127.0.0.1"
string(7) "1.2.3.4"

Warning: gethostbyname(): Host name is too long, the limit is %d characters in %s on line %d
bool(true)
