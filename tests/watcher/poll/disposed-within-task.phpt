--TEST--
Poll can be disposed while being awaited within a task.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
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

Task::asyncWithContext(Context::background(), function () use ($poll) {
    try {
        $poll->awaitReadable();
    } catch (\Throwable $e) {
        var_dump($e->getMessage());
    }
});

--EXPECT--
string(32) "Task scheduler has been disposed"
