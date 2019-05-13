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
]);

Task::async(function (\Iterator $it) {
    $it->rewind();
    var_dump($it->current());
    $it->next();
    var_dump($it->current());
}, $channel->getIterator());

(new Timer(10))->awaitTimeout();

var_dump($group->send('A', 50));
var_dump($group->send('B', 50));
var_dump($group->send('C', 50));

$channel->close();

--EXPECT--
string(1) "A"
int(0)
string(1) "B"
int(0)
NULL
