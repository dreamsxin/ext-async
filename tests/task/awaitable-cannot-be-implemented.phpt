--TEST--
Awaitable interface cannot be implemented by userland classes.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

class Foo implements Concurrent\Awaitable { }

?>
--EXPECTF--
Fatal error: Class Foo must not implement interface Concurrent\Awaitable, create an awaitable using Concurrent\Deferred instead in %s on line %d
