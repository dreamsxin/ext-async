--TEST--
Channel group constructor validates input channels.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

try {
    new ChannelGroup([123]);
} catch (\Throwable $e) {
    var_dump('ERROR');
}

try {
    new ChannelGroup([new class() { }]);
} catch (\Throwable $e) {
    var_dump('ERROR');
}

$obj = new class() implements \IteratorAggregate {
    public function getIterator() {
        return new \ArrayIterator([]);
    }
};

try {
    new ChannelGroup([$obj]);
} catch (\Throwable $e) {
    var_dump('ERROR');
}

$obj = new class() implements \IteratorAggregate {
    public function getIterator() {
        return 123;
    }
};

try {
    new ChannelGroup([$obj]);
} catch (\Throwable $e) {
    var_dump('ERROR');
}

--EXPECT--
string(5) "ERROR"
string(5) "ERROR"
string(5) "ERROR"
string(5) "ERROR"
