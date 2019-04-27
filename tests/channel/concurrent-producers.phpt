--TEST--
Channel multiple producers single consumer
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$producer = function (Channel $channel, int $max, bool $close) {
    try {
        $timer = new Timer(50);
    
        for ($i = 0; $i < $max; $i++) {
            $timer->awaitTimeout();
            
            $channel->send($i);
        }
    } finally {
        if ($close) {
            $channel->close();
        }
    }
};

$channel = new Channel();

Task::async($producer, $channel, 3, false);
Task::async($producer, $channel, 5, true);

foreach ($channel as $v) {
    var_dump($v);
}

--EXPECT--
int(0)
int(0)
int(1)
int(1)
int(2)
int(2)
int(3)
int(4)
