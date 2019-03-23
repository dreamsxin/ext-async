--TEST--
Channel buffered single consumer / producer
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$channel = new Channel(3);

Task::async(function () use ($channel) {
    try {
        for ($i = 0; $i < 5; $i++) {
            $channel->send($i);
        }
    } finally {
        $channel->close();
    }
});

foreach ($channel as $v) {
    var_dump($v);
}

$channel = new Channel(10);

Task::async(function () use ($channel) {
    try {
        for ($i = 0; $i < 5; $i++) {
            $channel->send($i);
        }
    } finally {
        $channel->close();
    }
});

foreach ($channel as $v) {
    var_dump($v);
}

--EXPECT--
int(0)
int(1)
int(2)
int(3)
int(4)
int(0)
int(1)
int(2)
int(3)
int(4)
