<?php

namespace Concurrent\Network;

list ($a, $b) = TcpSocket::pair();

\Concurrent\Task::async(function () use ($a) {
    try {
        $a->write('Hello World :)');
    } finally {
        $a->close();
    }
});

var_dump($b->read());
