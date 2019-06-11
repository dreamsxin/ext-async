--TEST--
Channel select blocks as needed.
--SKIPIF--
<?php require __DIR__ . '/skipif.inc'; ?>
--FILE--
<?php

namespace Concurrent;

$producer = function (Channel $channel, string $item, int $delay, int $skip) {
    try {
        if ($skip > 0) {
            (new Timer($skip))->awaitTimeout();
        }
        
        $timer = new Timer($delay);
    
        for ($i = 0; $i < 3; $i++) {
            $timer->awaitTimeout();
            $channel->send($item . $i);
        }
    } finally {
        $channel->close();
    }
};

$channels = [
    'A' => new Channel(),
    'B' => new Channel()
];

foreach ($channels as $k => $v) {
    Task::async($producer, $v, $k, 100, ($k == 'A') ? 0 : 150);
}

$group = new ChannelGroup($channels, true);

do {
    $val = $group->select();
    
    if ($val !== null) {
        var_dump($val->key, $val->value);
    }
} while ($group->count());

--EXPECT--
string(1) "A"
string(2) "A0"
string(1) "A"
string(2) "A1"
string(1) "B"
string(2) "B0"
string(1) "A"
string(2) "A2"
string(1) "B"
string(2) "B1"
string(1) "B"
string(2) "B2"
