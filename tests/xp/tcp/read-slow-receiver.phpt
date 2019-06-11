--TEST--
XP socket TCP can use unbuffered slowed down reads.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

require_once __DIR__ . '/assets/functions.php';

list ($a, $b) = socketpair();

var_dump(stream_set_read_buffer($a, 0));

Task::async(function () use ($b) {
	$chunk = str_repeat('A', 8192 * 2);
    
    for ($i = 0; $i < 100; $i++) {
    	fwrite($b, $chunk);
    }
    
    fclose($b);
});

$timer = new Timer(1);
$len = 0;

do {
	$timer->awaitTimeout();

	$chunk = fread($a, 4000);
	
	if ($chunk === false) {
		break;
	}
	
	$len += strlen($chunk);
} while (!feof($a));

var_dump($len == (8192 * 100 * 2));

--EXPECT--
int(0)
bool(true)
