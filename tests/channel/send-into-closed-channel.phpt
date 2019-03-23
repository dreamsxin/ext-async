--TEST--
Channel cannot send message after being closed.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$channel = new Channel();
$channel->close(new \Error('FOO'));

try {
    $channel->send(1);
} catch (ChannelClosedException $e) {
    var_dump($e->getPrevious()->getMessage());
}

--EXPECT--
string(3) "FOO"
