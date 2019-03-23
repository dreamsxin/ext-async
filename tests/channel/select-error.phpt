--TEST--
Channel select will forward error from input channel.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$group = new ChannelGroup([
    $channel = new Channel()
]);

$channel->close(new \Error('FAIL!'));

try {
    $group->select();
} catch (ChannelClosedException $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}

var_dump($group->select());

$group = new ChannelGroup([
    $channel = new Channel()
]);

Task::async(function () use ($channel) {
    $channel->close(new \Error('FAIL!'));
});

try {
    $group->select();
} catch (ChannelClosedException $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}

var_dump($group->select());

$group = new ChannelGroup([
    $c1 = new Channel(4),
    $c2 = new Channel()
]);

$c1->send('OK');
$c1->close();

$c2->close(new \Error('FAIL!'));

try {
    $group->select();
} catch (ChannelClosedException $e) {
    var_dump($e->getMessage());
    var_dump($e->getPrevious()->getMessage());
}

$v = null;
var_dump($group->select($v));
var_dump($v);

var_dump($group->select($v));
var_dump($v);

--EXPECT--
string(23) "Channel has been closed"
string(5) "FAIL!"
NULL
string(23) "Channel has been closed"
string(5) "FAIL!"
NULL
string(23) "Channel has been closed"
string(5) "FAIL!"
int(0)
string(2) "OK"
NULL
NULL
