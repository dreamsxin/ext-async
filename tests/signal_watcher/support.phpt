--TEST--
Signal watcher can check signal support.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

var_dump(SignalWatcher::isSupported(-1));
var_dump(SignalWatcher::isSupported(0));
var_dump(SignalWatcher::isSupported(SignalWatcher::SIGINT));
var_dump(SignalWatcher::isSupported(300));

--EXPECT--
bool(false)
bool(false)
bool(true)
bool(false)
