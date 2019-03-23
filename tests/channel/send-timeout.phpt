--TEST--
Channel group enforces timeout on send.
--SKIPIF--
<?php
if (!extension_loaded('task')) echo 'Test requires the task extension to be loaded';
?>
--FILE--
<?php

namespace Concurrent;

$group = new ChannelGroup([
    $channel = new Channel()    
], 50);

Task::async(function (\Iterator $it) {
    $it->rewind();
    var_dump($it->current());
    $it->next();
    var_dump($it->current());
}, $channel->getIterator());

(new Timer(10))->awaitTimeout();

var_dump($group->send('A'));
var_dump($group->send('B'));
var_dump($group->send('C'));

$channel->close();

--EXPECT--
string(1) "A"
int(0)
string(1) "B"
int(0)
NULL
