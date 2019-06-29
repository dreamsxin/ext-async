--TEST--
DNS gethostbynamel.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--INI--
async.dns=1
--FILE--
<?php

namespace Concurrent;

var_dump(gethostbynamel(gethostname()));
var_dump(gethostbynamel('localhost'));
var_dump(gethostbynamel('1.2.3.4'));

$a = str_repeat('A', 2048);
var_dump(gethostbynamel($a) == $a);

--EXPECTF--
array(1) {
  [0]=>
  string(%d) "%s.%s.%s.%s"
}
array(1) {
  [0]=>
  string(9) "127.0.0.1"
}
array(1) {
  [0]=>
  string(7) "1.2.3.4"
}

Warning: gethostbynamel(): Host name is too long, the limit is %d characters in %s on line %d
bool(true)
