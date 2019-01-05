--TEST--
Channel forwards close reason to receiver.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$channel = new Channel();
$channel->close(new \Error('FOO!'));

try {
    var_dump(iterator_to_array($channel->getIterator()));
} catch (ChannelClosedException $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}

--EXPECT--
string(23) "Channel has been closed"
string(4) "FOO!"
