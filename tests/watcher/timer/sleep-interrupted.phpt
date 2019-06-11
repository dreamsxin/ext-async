--TEST--
Sleep interrupted.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; if (DIRECTORY_SEPARATOR == '\\') die('skip'); ?>
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
