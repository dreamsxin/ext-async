--TEST--
Writable memory stream can be used.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Stream;

$stream = new WritableMemoryStream();

var_dump($stream->isClosed());
var_dump($stream->getContents());

$stream->write('foo');
var_dump($stream->getContents());

$stream->write('bar');
var_dump($stream->getContents());

$stream->close();
var_dump($stream->isClosed());

try {
    $stream->write('baz');
} catch (StreamClosedException $e) {
    var_dump($e->getMessage());
}

$stream = new WritableMemoryStream();
$stream->close(new \Error('FOO!'));

try {
    $stream->write('baz');
} catch (StreamClosedException $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}

--EXPECT--
bool(false)
string(0) ""
string(3) "foo"
string(6) "foobar"
bool(true)
string(29) "Cannot write to closed stream"
string(29) "Cannot write to closed stream"
string(4) "FOO!"
