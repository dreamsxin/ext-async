--TEST--
Task scheduler poll API.
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
            $chunk = fread($socket, 256);
            
            if ('' !== $chunk) {
                var_dump($chunk);
            }
        }
        
        if ($events & PollEvent::DISCONNECT) {
            $poll->stop();
        }
    });
    $poll->start(PollEvent::READABLE);
    $poll->ref();
    $poll->unref();
    
    fwrite($a, 'Hello');
    fclose($a);
    
    (new Timer(50))->awaitTimeout();
    
    $poll->start(PollEvent::READABLE);
    
    var_dump('World');
});

?>
--EXPECT--
string(5) "Hello"
string(5) "World"
