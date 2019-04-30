--TEST--
Signal can check signal support.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

var_dump(Signal::isSupported(-1));
var_dump(Signal::isSupported(0));
var_dump(Signal::isSupported(Signal::SIGINT));
var_dump(Signal::isSupported(300));

--EXPECT--
bool(false)
bool(false)
bool(true)
bool(false)
