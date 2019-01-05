--TEST--
Channel unbuffered single consumer / producer
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$channel = new Channel();

Task::async(function (\Iterator $it) {
    foreach ($it as $v) {
        var_dump($v);
    }
}, $channel->getIterator());

for ($i = 0; $i < 5; $i++) {
    $channel->send($i);
}

$channel->close();

--EXPECT--
int(0)
int(1)
int(2)
int(3)
int(4)
