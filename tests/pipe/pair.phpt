--TEST--
Pipe can be created as connected pair.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent\Network;

use Concurrent\Task;

list ($a, $b) = Pipe::pair();

var_dump($a->getPort());
var_dump($a->getRemotePort());
var_dump($a->setOption(1, 'foo'));

var_dump($a->isAlive());
var_dump($b->isAlive());

Task::async(function () use ($a) {
    try {
        try {
            $stream = $a->getReadableStream();
            var_dump($stream->read());
        } finally {
            $stream->close();
        }
        
        $a->writeAsync('World!');
        $a->flush();
        
        var_dump($a->getWriteQueueSize());
    } finally {
        $a->close();
    }
});

try {
    $b->getWritableStream()->write('Hello');
    
    while (null !== ($chunk = $b->read())) {
        var_dump($chunk);
    }
} finally {
    $b->close();
}

--EXPECT--
NULL
NULL
bool(false)
bool(true)
bool(true)
string(5) "Hello"
int(0)
string(6) "World!"
