--TEST--
Task keeps loop running while awaiting background task.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$t = Task::asyncWithContext(Context::background(), function () {
	(new Timer(200))->awaitTimeout();
	
	return 321;
});

var_dump('START');
var_dump(Task::await($t));
var_dump('DONE');

--EXPECT--
string(5) "START"
int(321)
string(4) "DONE"
