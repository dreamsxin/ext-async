--TEST--
Channel unbuffered single consumer / producer
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

call_user_func(function () {
    $channel = new Channel();

    Task::async(function (\Iterator $it) {
        foreach ($it as $v) {
            var_dump($v);
        }
    }, $channel->getIterator());

    for ($i = 0; $i < 5; $i++) {
        $channel->send($i);
    }
});

--EXPECT--
int(0)
int(1)
int(2)
int(3)
int(4)
