--TEST--
XP socket TCP connection.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

require_once __DIR__ . '/assets/functions.php';

list ($a, $b) = socketpair();

Task::async(function () use ($b) {
    try {
        var_dump('SEND');
        fwrite($b, "Hello");
    } finally {
        fclose($b);
    }
});

try {
    (new Timer(50))->awaitTimeout();

	$r = [$a];
	$w = [];
	$e = [];
	
	var_dump('SELECT');
	var_dump(stream_select($r, $w, $e, 1));
	var_dump(count($r));
    var_dump(stream_get_contents($a));
} finally {
    fclose($a);
}

--EXPECT--
string(4) "SEND"
string(6) "SELECT"
int(1)
int(1)
string(5) "Hello"
