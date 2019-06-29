--TEST--
Task scheduler can poll sockets.
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

TaskScheduler::register(TaskScheduler::class, function (TaskScheduler $scheduler) {
    return $scheduler;
});

TaskScheduler::run(function () {
    $scheduler = TaskScheduler::get(TaskScheduler::class);
    
    list ($a, $b) = pair();
    
    $poll = $scheduler->poll($b, function (int $events, $socket, PollEvent $poll) {
        if ($events & PollEvent::READABLE) {
            if ($chunk = fread($socket, 256)) {
                var_dump($chunk);
            }
        }
        
        if ($events & PollEvent::DISCONNECT) {
            $poll->close();
            fclose($socket);
        }
    });
    $poll->start(PollEvent::READABLE);
    
    Task::async(function () use ($a, $scheduler) {
        fwrite($a, 'Hello');
        
        (new Timer(100))->awaitTimeout();
        
        fwrite($a, 'World');
        
        $scheduler->poll($a, function (int $events, $socket, PollEvent $poll) {
            $poll->close();
            fclose($socket);
        })->start(PollEvent::WRITABLE);
    });
});

?>
--EXPECT--
string(5) "Hello"
string(5) "World"
