--TEST--
Readable memory stream can be used.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent\Stream;

$stream = new ReadableMemoryStream();

var_dump($stream->isClosed());
var_dump($stream->read());

try {
    $stream->read(-1);
} catch (StreamException $e) {
    var_dump($e->getMessage());
}

$stream->close();
$stream->close();
var_dump($stream->isClosed());

try {
    $stream->read();
} catch (StreamClosedException $e) {
    var_dump($e->getMessage());
}

$stream = new ReadableMemoryStream('Hello World');

try {
    while (null !== ($chunk = $stream->read(8))) {
        var_dump($chunk);
    }
} finally {
    $stream->close(new \Error('FOO!'));
}

try {
    $stream->read();
} catch (StreamClosedException $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}

--EXPECT--
bool(false)
NULL
string(23) "Invalid read length: -1"
bool(true)
string(30) "Cannot read from closed stream"
string(8) "Hello Wo"
string(3) "rld"
string(30) "Cannot read from closed stream"
string(4) "FOO!"
