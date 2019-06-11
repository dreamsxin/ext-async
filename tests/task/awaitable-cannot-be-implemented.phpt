--TEST--
Awaitable interface cannot be implemented by userland classes.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

class Foo implements Concurrent\Awaitable { }

?>
--EXPECTF--
Fatal error: Interface Concurrent\Awaitable cannot be implemented by class Foo in %s on line %d
