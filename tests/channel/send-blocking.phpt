--TEST--
Channel group send blocks as needed.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$consumer = function (iterable $it) {
    foreach ($it as $v) {
        var_dump($v);
    }
};

$channels = [
    'A' => new Channel(),
    'B' => new Channel(2)
];

foreach ($channels as $k => $v) {
    Task::async($consumer, $v);
}

$group = new ChannelGroup($channels);

for ($i = 0; $i < 5; $i++) {
    var_dump($group->send($i));
}

foreach ($channels as $v) {
    $v->close();
}

--EXPECT--
string(1) "B"
string(1) "B"
string(1) "A"
int(2)
string(1) "A"
int(3)
string(1) "A"
int(4)
int(0)
int(1)
