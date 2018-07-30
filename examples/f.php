<?php

namespace Concurrent;

list ($a, $b) = stream_socket_pair(\STREAM_PF_UNIX, \STREAM_SOCK_STREAM, \STREAM_IPPROTO_IP);

foreach ([$a, $b] as $r) {
    stream_set_blocking($r, false);
    stream_set_read_buffer($r, 0);
    stream_set_write_buffer($r, 0);
}

$watcher = new Watcher($b);

$timer = new Timer(function () use ($a, $watcher) {
    fwrite($a, 'Hello Socket :)');
    fclose($a);
    var_dump('EOF!');

//     $watcher->close(new \LogicException('Nope!'));
});

$timer->start(500);

var_dump('WAIT FOR IO...');

$watcher->awaitReadable();

var_dump(stream_get_contents($b));
fclose($b);
