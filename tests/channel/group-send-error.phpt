--TEST--
Channel group send error forwarding.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$group = new ChannelGroup([
    $a = new Channel(),
    $b = new Channel()
]);

$a->close(new \Error('FOO'));
$b->close(new \Error('BAR'));

var_dump(count($group));

try {
    $group->send(0);
} catch (ChannelClosedException $e) {
    var_dump($e->getPrevious()->getMessage());
}

var_dump(count($group));

try {
    $group->send(0);
} catch (ChannelClosedException $e) {
    var_dump($e->getPrevious()->getMessage());
}

var_dump(count($group));

--EXPECT--
int(2)
string(3) "FOO"
int(1)
string(3) "BAR"
int(0)
