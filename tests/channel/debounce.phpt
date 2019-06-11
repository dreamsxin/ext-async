--TEST--
Channel can be combined with group to debounce events.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$channel = new Channel();

Task::async(function () use ($channel) {
    $e = null;
    
    try {
        $timer = new Timer(50);
        
        for ($i = 0; $i < 3; $i++) {
            $channel->send($i);
            $channel->send(new class() {});
            
            $timer->awaitTimeout();
        }
    } catch (\Throwable $e) {
    } finally {
        $channel->close($e);
    }
});

$debounce = new ChannelGroup([$channel->getIterator()]);

foreach ($channel as $v) {
    while (null !== $debounce->select(10));
    
    var_dump($v);
}

--EXPECT--
int(0)
int(1)
int(2)
