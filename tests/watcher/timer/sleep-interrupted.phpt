--TEST--
Sleep interrupted.
--SKIPIF--
<?php
if (DIRECTORY_SEPARATOR == '\\') die('Test does not make sense on a Windows OS');
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--INI--
async.timer=1
--FILE--
<?php

namespace Concurrent;

Task::asyncWithContext(Context::background(), function () {
	var_dump(sleep(1));
});

--EXPECTF--
int(1)
