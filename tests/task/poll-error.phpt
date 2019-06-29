--TEST--
Task scheduler can deal with error thrown within poll callback.
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

try {
    TaskScheduler::run(function () {
        $scheduler = TaskScheduler::get(TaskScheduler::class);
        
        list ($a, $b) = pair();
        
        $poll = $scheduler->poll($b, function (int $events, $socket, PollEvent $poll) {
            throw new \Error('FOO!');
        });
        $poll->start(PollEvent::READABLE);
        
        fclose($a);
    });
} catch (\Throwable $e) {
    var_dump($e->getMessage());
}

list ($a, $b) = pair();

TaskScheduler::get(TaskScheduler::class)->poll($b, function () {
    var_dump('END');
    exit();
})->start(PollEvent::READABLE);

fclose($a);

?>
--EXPECT--
string(4) "FOO!"
string(3) "END"
