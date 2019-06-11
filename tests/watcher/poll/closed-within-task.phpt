--TEST--
Poll can be closed while being awaited within a task.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

function pair()
{
    return array_map(function ($s) {
        stream_set_blocking($s, false);
        stream_set_read_buffer($s, 0);
        stream_set_write_buffer($s, 0);
        
        return $s;
    }, stream_socket_pair((DIRECTORY_SEPARATOR == '\\') ? STREAM_PF_INET : STREAM_PF_UNIX, STREAM_SOCK_STREAM, STREAM_IPPROTO_IP));
}

list ($a, $b) = pair();

$poll = new Poll($b);

Task::async(function () use ($poll) {
    try {
        $poll->awaitReadable();
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
        var_dump($e->getPrevious()->getMessage());
    }

    try {
        $poll->awaitReadable();
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
        var_dump($e->getPrevious()->getMessage());
    }
    
    try {
        $poll->awaitWritable();
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
        var_dump($e->getPrevious()->getMessage());
    }
});

(new Timer(10))->awaitTimeout();

$poll->close(new \Error('FAIL!'));

--EXPECT--
string(20) "Poll has been closed"
string(5) "FAIL!"
string(20) "Poll has been closed"
string(5) "FAIL!"
string(20) "Poll has been closed"
string(5) "FAIL!"
